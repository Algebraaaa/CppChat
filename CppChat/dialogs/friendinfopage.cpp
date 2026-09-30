#include "friendinfopage.h"
#include "ui_friendinfopage.h"
#include "network/usermgr.h"

FriendInfoPage::FriendInfoPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::FriendInfoPage),_user_info(nullptr)
{
    ui->setupUi(this);
    ui->msg_chat->SetState("normal","hover","press");
    setAttribute(Qt::WA_StyledBackground);
    ui->contact_card->setAttribute(Qt::WA_StyledBackground);
    ui->nick_row->setAttribute(Qt::WA_StyledBackground);
    ui->remark_row->setAttribute(Qt::WA_StyledBackground);
    ui->desc_row->setAttribute(Qt::WA_StyledBackground);
}

FriendInfoPage::~FriendInfoPage()
{
    delete ui;
}

void FriendInfoPage::SetInfo(std::shared_ptr<UserInfo> user_info)
{
    _user_info = user_info;
    if (!user_info) {
        ui->icon_lb->setProperty("avatar_uid", 0);
        ui->icon_lb->clear();
        ui->name_lb->clear();
        ui->nick_lb->setText(tr("未设置"));
        ui->bak_lb->setText(tr("未设置"));
        ui->desc_lb->setText(tr("暂无个人描述"));
        ui->sex_lb->clear();
        ui->msg_chat->setEnabled(false);
        return;
    }
    ui->msg_chat->setEnabled(true);
    UserMgr::GetInstance()->RefreshAvatar(user_info->_uid);
    UserMgr::GetInstance()->AttachAvatarLabel(ui->icon_lb, user_info->_uid, user_info->_icon);

    ui->name_lb->setText(user_info->_name);
    ui->nick_lb->setText(user_info->_nick.trimmed().isEmpty() ? tr("未设置") : user_info->_nick);
    ui->bak_lb->setText(user_info->_back.trimmed().isEmpty() ? tr("未设置") : user_info->_back);
    ui->desc_lb->setText(user_info->_desc.trimmed().isEmpty() ? tr("暂无个人描述") : user_info->_desc);
    ui->sex_lb->setText(user_info->_sex == 1 ? tr("男") : (user_info->_sex == 2 ? tr("女") : QString()));
}

void FriendInfoPage::on_msg_chat_clicked()
{
    if (!_user_info) return;
    emit sig_jump_chat_item(_user_info);
}
