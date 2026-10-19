#include "chatuserwid.h"
#include "network/usermgr.h"
#include "ui_chatuserwid.h"
#include <QDateTime>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QStyle>

ChatUserWid::ChatUserWid(QWidget *parent) : ListItemBase(parent), ui(new Ui::ChatUserWid)
{
  ui->setupUi(this);
  ui->user_name_lb->setTextFormat(Qt::PlainText);
  SetItemType(ListItemType::CHAT_USER_ITEM);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  ui->time_lb->clear();
  ui->pin_lb->setPixmap(
      QPixmap(":/icons/conversation-pin.svg")
          .scaled(ui->pin_lb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  ui->pin_lb->hide();
  delete ui->verticalLayout_2->takeAt(1);
  auto *indicators = new QHBoxLayout;
  indicators->setContentsMargins(0, 0, 0, 0);
  indicators->setSpacing(4);
  indicators->addStretch();
  _muteIcon = new QLabel(this);
  _muteIcon->setObjectName("conversationMuteIcon");
  _muteIcon->setFixedSize(16, 16);
  _muteIcon->setToolTip(tr("消息免打扰"));
  _muteIcon->setPixmap(QPixmap(":/icons/conversation-bell-off.svg")
                           .scaled(16, 16, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  _muteIcon->hide();
  indicators->addWidget(_muteIcon);
  indicators->addWidget(ui->pin_lb);
  ui->verticalLayout_2->addLayout(indicators);
  ui->icon_lb->setScaledContents(true);
  ui->user_name_lb->installEventFilter(this);
  ui->user_chat_lb->installEventFilter(this);
}

ChatUserWid::~ChatUserWid() { delete ui; }

QSize ChatUserWid::sizeHint() const
{
  // 列表条目只固定高度，宽度跟随左侧会话栏伸缩。
  return QSize(1, 76);
}

void ChatUserWid::SetInfo(QString name, QString head, QString msg)
{
  if (_name == name && _head == head && _msg == msg)
    return;
  _name = name;
  _head = head;
  _msg = msg;
  // 头像统一由 AttachAvatarLabel 的缩略图缓存和更新信号提供。
  UpdateVisibleText();
}

bool ChatUserWid::eventFilter(QObject *watched, QEvent *event)
{
  if ((watched == ui->user_name_lb || watched == ui->user_chat_lb) &&
      event->type() == QEvent::Resize) {
    UpdateVisibleText();
  }
  return ListItemBase::eventFilter(watched, event);
}

void ChatUserWid::UpdateVisibleText()
{
  const QString name = QFontMetrics(ui->user_name_lb->font())
                           .elidedText(_name, Qt::ElideRight, ui->user_name_lb->width());
  const QString preview = QFontMetrics(ui->user_chat_lb->font())
                              .elidedText(_msg, Qt::ElideRight, ui->user_chat_lb->width());
  if (ui->user_name_lb->text() != name)
    ui->user_name_lb->setText(name);
  if (ui->user_chat_lb->text() != preview)
    ui->user_chat_lb->setText(preview);
  ui->user_name_lb->setToolTip(_name);
  ui->user_chat_lb->setToolTip(_msg);
}

void ChatUserWid::SetChatData(std::shared_ptr<ChatThreadData> data)
{
  if (!data)
    return;
  const auto user = UserMgr::GetInstance()->GetFriendById(data->GetOtherId());
  const auto name = data->IsGroup() ? data->GetGroupName()
                    : user          ? (user->_back.isEmpty() ? user->_name : user->_back)
                                    : tr("用户 %1").arg(data->GetOtherId());
  const auto icon = data->IsGroup() ? QStringLiteral(":/icons/action-group-chat.svg")
                    : user          ? user->_icon
                                    : QStringLiteral(":/res/head_1.jpg");
  QString preview = data->GetLastMsg();
  if (data->IsGroup()) {
    int sender = 0;
    const auto pending = data->GetPendingMessages();
    if (!pending.empty())
      sender = pending.back()->GetSendUid();
    else if (!data->GetMsgMapRef().isEmpty() &&
             data->GetMsgMapRef().last()->GetMsgId() == data->GetLastMsgId())
      sender = data->GetMsgMapRef().last()->GetSendUid();
    if (sender > 0 && !preview.isEmpty()) {
      const auto friendInfo = UserMgr::GetInstance()->GetFriendById(sender);
      const auto senderName = sender == UserMgr::GetInstance()->GetUid() ? tr("我")
                              : friendInfo && !friendInfo->_back.isEmpty()
                                  ? friendInfo->_back
                                  : data->MemberName(sender);
      preview = senderName + QStringLiteral(": ") + preview;
    }
    if (preview.isEmpty())
      preview = tr("群聊 · %1 位成员").arg(data->Group()["members"].toArray().size());
  }
  SetInfo(name, icon, preview);
  const QString timestamp = data->GetLastChatTime();
  QDateTime chatTime = QDateTime::fromString(timestamp, "yyyy-MM-dd HH:mm:ss.zzz");
  if (!chatTime.isValid())
    chatTime = QDateTime::fromString(timestamp, "yyyy-MM-dd HH:mm:ss");
  if (chatTime.isValid()) {
    const QDate today = QDate::currentDate();
    const QDate day = chatTime.date();
    const QString displayTime = day == today                 ? chatTime.toString("HH:mm")
                                : day == today.addDays(-1)   ? tr("昨天")
                                : day.year() == today.year() ? chatTime.toString("MM-dd")
                                                             : chatTime.toString("yy/MM/dd");
    ui->time_lb->setText(displayTime);
    ui->time_lb->setToolTip(chatTime.toString("yyyy-MM-dd HH:mm:ss"));
  } else {
    ui->time_lb->clear();
    ui->time_lb->setToolTip({});
  }
  UserMgr::GetInstance()->AttachAvatarLabel(ui->icon_lb, data->GetOtherId(), icon);
}

void ChatUserWid::SetSearchInfo(const std::shared_ptr<UserInfo> &user, const QString &preview)
{
  if (!user)
    return;

  const QString name = user->_back.isEmpty() ? user->_name : user->_back;
  SetInfo(name, user->_icon, preview);
  ui->time_lb->clear();
  ui->pin_lb->hide();
  ui->time_lb->setToolTip({});
  UserMgr::GetInstance()->AttachAvatarLabel(ui->icon_lb, user->_uid, user->_icon);
}
void ChatUserWid::ShowRedPoint(bool show)
{
  if (ui->unread_dot->isHidden() == show) {
    ui->unread_dot->setVisible(show);
    if (show)
      ui->unread_dot->raise();
  }
}

void ChatUserWid::SetSelected(bool selected)
{
  if (_selected == selected)
    return;
  _selected = selected;
  for (auto *label : {ui->user_name_lb, ui->user_chat_lb, ui->time_lb}) {
    label->setProperty("selected", selected);
    label->style()->unpolish(label);
    label->style()->polish(label);
  }
}

void ChatUserWid::SetPinned(bool pinned)
{
  if (ui->pin_lb->isHidden() == pinned)
    ui->pin_lb->setVisible(pinned);
}

void ChatUserWid::SetMuted(bool muted)
{
  _muteIcon->setVisible(muted);
  setProperty("muted", muted);
  ui->time_lb->setToolTip(muted ? tr("消息免打扰") : QString());
  ui->user_name_lb->setAccessibleDescription(muted ? tr("消息免打扰") : QString());
}
