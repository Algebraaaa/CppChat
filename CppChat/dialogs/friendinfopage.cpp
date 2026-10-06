#include "friendinfopage.h"
#include "applyfriend.h"
#include "ui_friendinfopage.h"
#include "network/usermgr.h"
#include "network/tcpmgr.h"
#include <QJsonDocument>
#include <QMessageBox>
#include <QMenu>
#include <QResizeEvent>
#include <QToolButton>

FriendInfoPage::FriendInfoPage(QWidget *parent)
    : QWidget(parent), ui(new Ui::FriendInfoPage), _user_info(nullptr)
{
  // 在创建子控件前设置样式作用域，避免子控件首次 polish 时匹配到旧对象名。
  setObjectName("friend_info_page");
  ui->setupUi(this);
  ui->sex_lb->hide();
  setAttribute(Qt::WA_StyledBackground);
  ui->profile_scroll->viewport()->installEventFilter(this);
  ui->msg_chat->setIcon(QIcon(":/icons/contact-chat.svg"));
  ui->voice_call_btn->setIcon(QIcon(":/icons/contact-phone.svg"));
  ui->video_call_btn->setIcon(QIcon(":/icons/contact-video.svg"));
  ui->more_btn->setIcon(QIcon(":/icons/ellipsis-normal.svg"));
  ui->more_btn->setIconSize(QSize(30, 30));
  auto *menu = new QMenu(this);
  menu->setObjectName("contact_more_menu");
  // 透明无框窗口让 QSS 圆角生效，同时去掉 Windows 的方形菜单框和硬阴影。
  menu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
  menu->setAttribute(Qt::WA_TranslucentBackground);
  for (const auto &title : {tr("设置备注"), tr("设为星标朋友"), tr("朋友权限")}) {
    auto *action = menu->addAction(title);
    connect(action, &QAction::triggered, this,
            [this, title] { emit sig_show_placeholder(title, tr("%1功能暂未开放").arg(title)); });
  }
  auto *tagsAction = menu->addAction(tr("设置标签"));
  connect(tagsAction, &QAction::triggered, this, [this] {
    if (!_user_info || _deletingUid != 0)
      return;
    ApplyFriend dialog(window());
    dialog.SetContactInfo(_user_info);
    dialog.exec();
  });
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_contact_tags_updated, this, [this](int uid) {
    if (_user_info && uid == _user_info->_uid)
      SetInfo(_user_info);
  });
  menu->addSeparator();
  _deleteFriendAction = menu->addAction(tr("删除好友"));
  _deleteFriendAction->setObjectName("delete_friend_action");
  connect(_deleteFriendAction, &QAction::triggered, this, &FriendInfoPage::DeleteFriend);
  connect(ui->more_btn, &QPushButton::clicked, this, [this, menu] {
    menu->popup(ui->more_btn->mapToGlobal(QPoint(0, ui->more_btn->height())));
  });
  for (auto *button :
       {ui->permission_btn, ui->moments_btn, ui->channels_btn, ui->common_groups_btn}) {
    QString title;
    if (button == ui->permission_btn)
      title = tr("朋友权限");
    else if (button == ui->moments_btn)
      title = tr("朋友圈");
    else if (button == ui->channels_btn)
      title = tr("视频号");
    else
      title = tr("共同群聊");
    connect(button, &QPushButton::clicked, this,
            [this, title] { emit sig_show_placeholder(title, tr("%1功能暂未开放").arg(title)); });
  }
  for (auto *button : {ui->voice_call_btn, ui->video_call_btn})
    connect(button, &QToolButton::clicked, this, [this, button] {
      emit sig_show_placeholder(button->text(), tr("%1功能暂未开放").arg(button->text()));
    });
  ui->delete_status->hide();
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_friend_deleted, this, [this](int uid) {
    if (uid != _deletingUid)
      return;
    _deletingUid = 0;
    _deleteFriendAction->setEnabled(_user_info != nullptr && _user_info->_uid != uid);
    ui->delete_status->hide();
  });
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_request_failed, this,
          [this](ReqId request, const QString &message) {
            if (request != ID_DELETE_FRIEND_REQ || _deletingUid == 0)
              return;
            _deletingUid = 0;
            _deleteFriendAction->setEnabled(_user_info != nullptr);
            ui->delete_status->setText(message);
            ui->delete_status->show();
          });
}

FriendInfoPage::~FriendInfoPage()
{
  delete ui;
}

bool FriendInfoPage::eventFilter(QObject *watched, QEvent *event)
{
  if (watched == ui->profile_scroll->viewport() && event->type() == QEvent::Resize) {
    const int available = static_cast<QResizeEvent *>(event)->size().width() - 56;
    ui->contact_card->setFixedWidth(qMax(280, qMin(480, available)));
  }
  return QWidget::eventFilter(watched, event);
}

void FriendInfoPage::SetInfo(std::shared_ptr<UserInfo> user_info)
{
  _user_info = user_info;
  ui->delete_status->hide();
  _deleteFriendAction->setEnabled(user_info != nullptr && _deletingUid == 0);
  ui->more_btn->setEnabled(user_info != nullptr);
  if (!user_info) {
    ui->icon_lb->setProperty("avatar_uid", 0);
    ui->icon_lb->clear();
    ui->name_lb->clear();
    ui->nick_lb->setText(tr("昵称：未设置"));
    ui->account_lb->setText(tr("账号：—"));
    ui->bak_lb->setText(tr("未设置"));
    ui->tags_lb->setText(tr("未设置"));
    ui->desc_lb->setText(tr("暂无个人描述"));
    ui->sex_lb->clear();
    ui->sex_lb->hide();
    ui->msg_chat->setEnabled(false);
    return;
  }
  ui->msg_chat->setEnabled(true);
  UserMgr::GetInstance()->RefreshAvatar(user_info->_uid);
  UserMgr::GetInstance()->AttachAvatarLabel(ui->icon_lb, user_info->_uid, user_info->_icon);

  ui->name_lb->setText(user_info->_back.trimmed().isEmpty() ? user_info->_name : user_info->_back);
  ui->nick_lb->setText(
      tr("昵称：%1").arg(user_info->_nick.trimmed().isEmpty() ? tr("未设置") : user_info->_nick));
  ui->account_lb->setText(tr("账号：%1").arg(user_info->_name));
  ui->bak_lb->setText(user_info->_back.trimmed().isEmpty() ? tr("未设置") : user_info->_back);
  ui->tags_lb->setText(user_info->_tags.isEmpty() ? tr("未设置") : user_info->_tags.join(QStringLiteral("、")));
  ui->desc_lb->setText(user_info->_desc.trimmed().isEmpty() ? tr("暂无个人描述")
                                                            : user_info->_desc);
  const QString sexText =
      user_info->_sex == 1 ? tr("男") : (user_info->_sex == 2 ? tr("女") : QString());
  ui->sex_lb->setText(sexText);
  ui->sex_lb->setVisible(!sexText.isEmpty());
}

void FriendInfoPage::Reset()
{
  _deletingUid = 0;
  SetInfo(nullptr);
}

void FriendInfoPage::on_msg_chat_clicked()
{
  if (!_user_info)
    return;
  emit sig_jump_chat_item(_user_info);
}

void FriendInfoPage::DeleteFriend()
{
  if (!_user_info || _deletingUid != 0)
    return;
  const int uid = _user_info->_uid;
  const QString name = _user_info->_back.isEmpty() ? _user_info->_name : _user_info->_back;
  const auto answer = QMessageBox::question(
      this, tr("删除好友"),
      tr("确定删除好友“%1”？\n双方好友关系将解除，原聊天记录保留，重新添加后可恢复会话。")
          .arg(name),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (answer != QMessageBox::Yes || !UserMgr::GetInstance()->CheckFriendById(uid))
    return;
  _deletingUid = uid;
  _deleteFriendAction->setEnabled(false);
  ui->delete_status->setText(tr("正在删除好友…"));
  ui->delete_status->show();
  const QJsonObject request{{"friend_uid", uid}};
  emit TcpMgr::GetInstance()
      -> sig_send_data(ID_DELETE_FRIEND_REQ, QJsonDocument(request).toJson(QJsonDocument::Compact));
}
