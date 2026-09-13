#include "findsuccessdlg.h"
#include "applyfriend.h"
#include "network/usermgr.h"
#include "ui_findsuccessdlg.h"
#include <QPushButton>

FindSuccessDlg::FindSuccessDlg(QWidget *parent) : QDialog(parent), ui(new Ui::FindSuccessDlg), _parent(parent)
{
  ui->setupUi(this);
  setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
  setObjectName("FindSuccessDlg");
  ui->add_friend_btn->SetState("normal", "hover", "press");
  auto *close = new QPushButton(QStringLiteral("×"), this);
  close->setObjectName("close_btn");
  close->setGeometry(width() - 30, 2, 28, 28);
  close->setToolTip(tr("关闭"));
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
}
FindSuccessDlg::~FindSuccessDlg() { delete ui; }
void FindSuccessDlg::SetSearchInfo(std::shared_ptr<SearchInfo> info)
{
  _si = info;
  ui->name_lb->setText(info->_name);
  QPixmap icon(info->_icon);
  if (icon.isNull()) icon.load(":/res/head_1.jpg");
  ui->head_lb->setPixmap(icon.scaled(ui->head_lb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  const auto mgr = UserMgr::GetInstance();
  const bool self = info->_uid == mgr->GetUid();
  ui->add_friend_btn->setEnabled(!self);
  ui->add_friend_btn->setText(self ? tr("这是你自己") :
      (mgr->CheckFriendById(info->_uid) ? tr("发送消息") : tr("添加到通讯录")));
}
void FindSuccessDlg::on_add_friend_btn_clicked()
{
  if (!_si || _si->_uid == UserMgr::GetInstance()->GetUid()) return;
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
