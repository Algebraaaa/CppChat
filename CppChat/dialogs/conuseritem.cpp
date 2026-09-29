#include "conuseritem.h"
#include "ui_conuseritem.h"
#include "network/usermgr.h"

ConUserItem::ConUserItem(QWidget *parent) : ListItemBase(parent), ui(new Ui::ConUserItem)
{
  ui->setupUi(this);
  SetItemType(ListItemType::CONTACT_USER_ITEM);
  ui->red_point->raise();
  ShowRedPoint(false);
}

ConUserItem::~ConUserItem()
{
  delete ui;
}

QSize ConUserItem::sizeHint() const
{
  return QSize(250, 70); // 返回自定义的尺寸
}

void ConUserItem::SetInfo(std::shared_ptr<AuthInfo> auth_info)
{
  _info = std::make_shared<UserInfo>(auth_info);
  UserMgr::GetInstance()->AttachAvatarLabel(ui->icon_lb, _info->_uid, _info->_icon);
  ui->icon_lb->setScaledContents(true);

  ui->user_name_lb->setText(_info->_name);
}

void ConUserItem::SetInfo(int uid, QString name, QString icon)
{
  _info = std::make_shared<UserInfo>(uid, name, name, icon, 0);

  UserMgr::GetInstance()->AttachAvatarLabel(ui->icon_lb, _info->_uid, _info->_icon);
  ui->icon_lb->setScaledContents(true);

  ui->user_name_lb->setText(_info->_name);
}

void ConUserItem::SetInfo(std::shared_ptr<AuthRsp> auth_rsp)
{
  _info = std::make_shared<UserInfo>(auth_rsp);

  UserMgr::GetInstance()->AttachAvatarLabel(ui->icon_lb, _info->_uid, _info->_icon);
  ui->icon_lb->setScaledContents(true);

  ui->user_name_lb->setText(_info->_name);
}

void ConUserItem::ShowRedPoint(bool show)
{
  if (show) {
    ui->red_point->show();
  } else {
    ui->red_point->hide();
  }
}

std::shared_ptr<UserInfo> ConUserItem::GetInfo()
{
  return _info;
}

void ConUserItem::SetInfo(std::shared_ptr<UserInfo> user)
{
  _info = user;
  if (!user) return;
  UserMgr::GetInstance()->AttachAvatarLabel(ui->icon_lb, user->_uid, user->_icon);
  ui->icon_lb->setScaledContents(true);
  ui->user_name_lb->setText(user->_back.isEmpty() ? user->_name : user->_back);
}
