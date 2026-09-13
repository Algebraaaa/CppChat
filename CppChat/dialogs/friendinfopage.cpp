#include "friendinfopage.h"
#include "ui_friendinfopage.h"
#include <QDebug>

FriendInfoPage::FriendInfoPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::FriendInfoPage),_user_info(nullptr)
{
    ui->setupUi(this);
    ui->msg_chat->SetState("normal","hover","press");
    ui->video_chat->hide();
    ui->voice_chat->hide();
    setAttribute(Qt::WA_StyledBackground);
}

FriendInfoPage::~FriendInfoPage()
{
    delete ui;
}

void FriendInfoPage::SetInfo(std::shared_ptr<UserInfo> user_info)
{
    _user_info = user_info;
    if (!user_info) {
        ui->icon_lb->clear();
        ui->name_lb->clear();
        ui->nick_lb->clear();
        ui->bak_lb->clear();
        ui->sex_lb->clear();
        return;
    }
    // 加载图片
    QPixmap pixmap(user_info->_icon);
    if (pixmap.isNull()) pixmap.load(":/res/head_1.jpg");

    // 设置图片自动缩放
    ui->icon_lb->setPixmap(pixmap.scaled(ui->icon_lb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->icon_lb->setScaledContents(true);

    ui->name_lb->setText(user_info->_name);
    ui->nick_lb->setText(user_info->_nick);
    ui->bak_lb->setText(user_info->_back);
    ui->sex_lb->setText(user_info->_sex == 1 ? tr("男") : (user_info->_sex == 2 ? tr("女") : QString()));
}

void FriendInfoPage::on_msg_chat_clicked()
{
    qDebug() << "msg chat btn clicked";
    emit sig_jump_chat_item(_user_info);
}
