#include "chatsettingspanel.h"
#include "network/usermgr.h"
#include "ui_chatsettingspanel.h"
#include <QApplication>
#include <QGridLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

ChatSettingsPanel::ChatSettingsPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::ChatSettingsPanel),
      _animation(new QPropertyAnimation(this, "revealProgress", this))
{
  ui->setupUi(this);
  _groupPreview = new QWidget(this);
  _groupPreview->setObjectName("groupPreview");
  new QVBoxLayout(_groupPreview);
  ui->settings_layout->insertWidget(0, _groupPreview);
  _groupPreview->hide();
  setAttribute(Qt::WA_StyledBackground);
  setFocusPolicy(Qt::StrongFocus);
  hide();
  _animation->setEasingCurve(QEasingCurve::OutCubic);
  connect(_animation, &QPropertyAnimation::finished, this, [this] {
    if (!_openRequested)
      hide();
  });
  for (auto *button : {ui->add_member_btn, ui->find_content_btn, ui->clear_history_btn})
    button->setCursor(Qt::PointingHandCursor);
  ui->settings_chevron->setAttribute(Qt::WA_TransparentForMouseEvents);
  ui->settings_chevron->setPixmap(QPixmap(":/icons/contact-chevron-right.svg"));
  ui->settings_chevron->setScaledContents(true);
  connect(ui->add_member_btn, &QPushButton::clicked, this,
          [this] { emit sig_members_requested(); });
  connect(ui->find_content_btn, &QPushButton::clicked, this,
          &ChatSettingsPanel::sig_history_requested);
  connect(ui->clear_history_btn, &QPushButton::clicked, this,
          &ChatSettingsPanel::sig_clear_history);
  ui->mute_switch->setCursor(Qt::PointingHandCursor);
  ui->pin_switch->setCursor(Qt::PointingHandCursor);
  ui->mute_switch->setFixedSize(44, 28);
  ui->pin_switch->setFixedSize(44, 28);
  connect(ui->mute_switch, &QCheckBox::toggled, this, [this] {
    // 暂未接入消息通知，不让视觉开关误导用户以为已经生效。
    const QSignalBlocker blocker(ui->mute_switch);
    ui->mute_switch->setChecked(false);
    ShowPlaceholder(tr("消息免打扰"));
  });
  connect(ui->pin_switch, &QCheckBox::toggled, this, &ChatSettingsPanel::sig_pin_changed);
  qApp->installEventFilter(this);
}

ChatSettingsPanel::~ChatSettingsPanel()
{
  qApp->removeEventFilter(this);
  delete ui;
}

void ChatSettingsPanel::SetUser(const std::shared_ptr<UserInfo> &user)
{
  _groupPreview->hide();
  ui->members_row->show();
  ui->add_member_label->setText(tr("添加"));
  ui->add_member_btn->setAccessibleName(tr("发起群聊"));
  ui->add_member_btn->setToolTip(tr("创建群聊"));
  const QString name = user ? (user->_back.isEmpty() ? user->_name : user->_back) : tr("未知用户");
  ui->member_name->setText(
      ui->member_name->fontMetrics().elidedText(name, Qt::ElideRight, ui->member_name->width()));
  ui->member_name->setToolTip(name);
  UserMgr::GetInstance()->AttachAvatarLabel(ui->member_avatar, user ? user->_uid : 0,
                                            user ? user->_icon : QString());
}

void ChatSettingsPanel::SetPinned(bool pinned)
{
  const QSignalBlocker blocker(ui->pin_switch);
  ui->pin_switch->setChecked(pinned);
}

void ChatSettingsPanel::SetToggleButton(QWidget *button) { _toggleButton = button; }

void ChatSettingsPanel::SetAvailableRect(const QRect &rect)
{
  _availableRect = rect;
  setRevealProgress(_progress);
}

qreal ChatSettingsPanel::revealProgress() const { return _progress; }

void ChatSettingsPanel::setRevealProgress(qreal progress)
{
  _progress = qBound(qreal(0), progress, qreal(1));
  const int panelWidth = qMin(320, _availableRect.width());
  // 保持抽屉宽度不变，只平移；其余部分由父控件裁剪，不挤压聊天布局。
  setGeometry(_availableRect.x() + _availableRect.width() - qRound(panelWidth * _progress),
              _availableRect.y(), panelWidth, _availableRect.height());
}

void ChatSettingsPanel::AnimateTo(qreal target)
{
  _animation->stop();
  _animation->setDuration(qMax(1, qRound(260 * qAbs(target - _progress))));
  _animation->setStartValue(_progress);
  _animation->setEndValue(target);
  _animation->start();
}

void ChatSettingsPanel::Toggle()
{
  if (_openRequested) {
    Close();
    return;
  }
  _openRequested = true;
  show();
  raise();
  setFocus(Qt::OtherFocusReason);
  AnimateTo(1);
}

void ChatSettingsPanel::Close(bool animated)
{
  _openRequested = false;
  if (animated && isVisible()) {
    AnimateTo(0);
  } else {
    _animation->stop();
    setRevealProgress(0);
    hide();
  }
}

bool ChatSettingsPanel::eventFilter(QObject *watched, QEvent *event)
{
  if (_openRequested && isVisible()) {
    auto *widget = qobject_cast<QWidget *>(watched);
    if (widget && widget->window() == window()) {
      if (event->type() == QEvent::KeyPress &&
          static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        Close();
        return true;
      }
      if (event->type() == QEvent::MouseButtonPress && widget != this && !isAncestorOf(widget) &&
          widget != _toggleButton && (!_toggleButton || !_toggleButton->isAncestorOf(widget)))
        Close();
    }
  }
  // 不吞掉外侧点击：关闭面板后，用户仍可正常选择会话、编辑消息。
  return QWidget::eventFilter(watched, event);
}

void ChatSettingsPanel::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_Escape) {
    Close();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void ChatSettingsPanel::ShowPlaceholder(const QString &feature)
{
  QMessageBox::information(this, tr("功能建设中"),
                           tr("%1功能暂未实现，本次操作不会修改聊天数据。").arg(feature));
}

void ChatSettingsPanel::SetGroup(const std::shared_ptr<ChatThreadData> &group)
{
  if (!group)
    return;
  ui->members_row->hide();
  _groupPreview->show();
  auto metadata = group->Group();
  QJsonObject names;
  for (const auto &value : metadata["members"].toArray()) {
    const int uid = value.toObject()["uid"].toInt();
    const auto user = UserMgr::GetInstance()->GetFriendById(uid);
    names[QString::number(uid)] =
        user && !user->_back.isEmpty() ? user->_back : group->MemberName(uid);
  }
  metadata["display_names"] = names;
  if (metadata == _groupMetadata)
    return;
  _groupMetadata = metadata;
  auto *layout = qobject_cast<QVBoxLayout *>(_groupPreview->layout());
  while (auto *item = layout->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  auto *card = new QWidget(_groupPreview);
  auto *cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(0, 0, 0, 12);
  cardLayout->setSpacing(12);
  auto *title = new QLabel(tr("%1（%2）").arg(group->GetGroupName()).arg(names.size()), card);
  title->setTextFormat(Qt::PlainText);
  title->setWordWrap(true);
  title->setObjectName("groupPreviewTitle");
  cardLayout->addWidget(title);
  auto *grid = new QGridLayout;
  const auto members = metadata["members"].toArray();
  for (int i = 0; i < qMin(8, members.size()); ++i) {
    const int uid = members[i].toObject()["uid"].toInt();
    auto *member = new QToolButton(card);
    member->setText(names[QString::number(uid)].toString());
    member->setToolTip(member->text() +
                       (uid == metadata["owner_uid"].toInt() ? tr(" · 群主") : QString()));
    member->setAccessibleName(member->toolTip());
    member->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    member->setIconSize(QSize(36, 36));
    member->setFixedWidth(58);
    member->setCursor(Qt::PointingHandCursor);
    member->setIcon(QIcon(UserMgr::GetInstance()->AvatarThumbnail(uid, {}, QSize(36, 36))));
    connect(member, &QToolButton::clicked, this, &ChatSettingsPanel::sig_members_requested);
    connect(
        UserMgr::GetInstance().get(), &UserMgr::sig_avatar_ready, member, [member, uid](int ready) {
          if (ready == uid)
            member->setIcon(QIcon(UserMgr::GetInstance()->AvatarThumbnail(uid, {}, QSize(36, 36))));
        });
    grid->addWidget(member, i / 4, i % 4);
  }
  cardLayout->addLayout(grid);
  auto *manage = new QPushButton(tr("查看全部成员与群聊详情"), card);
  manage->setObjectName("groupDetailsButton");
  manage->setCursor(Qt::PointingHandCursor);
  connect(manage, &QPushButton::clicked, this, &ChatSettingsPanel::sig_members_requested);
  cardLayout->addWidget(manage);
  layout->addWidget(card);
}
