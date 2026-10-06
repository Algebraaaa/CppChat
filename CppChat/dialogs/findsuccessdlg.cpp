#include "findsuccessdlg.h"
#include "applyfriend.h"
#include "network/usermgr.h"
#include "ui_findsuccessdlg.h"
#include <QPushButton>

FindSuccessDlg::FindSuccessDlg(QWidget *parent)
    : QDialog(parent), ui(new Ui::FindSuccessDlg), _parent(parent)
{
  ui->setupUi(this);
  setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setObjectName("FindSuccessDlg");
  ui->dialog_title->setAttribute(Qt::WA_TransparentForMouseEvents);
  ui->add_friend_btn->SetState("normal", "hover", "press");
  connect(ui->close_btn, &QPushButton::clicked, this, &QDialog::reject);
}
FindSuccessDlg::~FindSuccessDlg()
{
  delete ui;
}
void FindSuccessDlg::SetSearchInfo(std::shared_ptr<SearchInfo> info)
{
  if (!info)
    return;
  _si = info;
  ui->name_lb->setText(
      tr("用户名：%1").arg(info->_name.trimmed().isEmpty() ? tr("未设置") : info->_name));
  ui->nick_lb->setText(
      tr("昵称：%1").arg(info->_nick.trimmed().isEmpty() ? tr("未设置") : info->_nick));
  ui->description_text->setPlainText(info->_desc.trimmed().isEmpty() ? tr("暂无个人描述")
                                                                     : info->_desc);
  UserMgr::GetInstance()->RefreshAvatar(info->_uid);
  UserMgr::GetInstance()->AttachAvatarLabel(ui->head_lb, info->_uid, info->_icon);
  const auto mgr = UserMgr::GetInstance();
  const bool self = info->_uid == mgr->GetUid();
  ui->add_friend_btn->setEnabled(!self);
  ui->add_friend_btn->setText(
      self ? tr("这是你自己")
           : (mgr->CheckFriendById(info->_uid) ? tr("发送消息") : tr("添加到通讯录")));
  adjustSize();
}
void FindSuccessDlg::on_add_friend_btn_clicked()
{
  if (!_si || _si->_uid == UserMgr::GetInstance()->GetUid())
    return;
  if (UserMgr::GetInstance()->CheckFriendById(_si->_uid)) {
    emit sig_jump_chat_item(_si);
  } else {
    auto *dialog = new ApplyFriend(_parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->SetSearchInfo(_si);
    dialog->open();
  }
  accept();
}
