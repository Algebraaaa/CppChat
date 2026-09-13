#include "userinfopage.h"
#include "ui_userinfopage.h"
#include "network/usermgr.h"
#include "widgets/imagecropperdialog.h"
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>
#include <QMessageBox>

UserInfoPage::UserInfoPage(QWidget *parent) : QWidget(parent), ui(new Ui::UserInfoPage)
{
  ui->setupUi(this);
  setAttribute(Qt::WA_StyledBackground);
  // 参考项目尚无资料修改/头像上传协议，资料只读，头像仅保存到本机。
  ui->nick_ed->setReadOnly(true);
  ui->name_ed->setReadOnly(true);
  ui->desc_ed->setReadOnly(true);
  ui->up_btn->setText(tr("更换本地头像"));
  ui->submit_btn->setText(tr("退出登录"));
  connect(ui->submit_btn, &QPushButton::clicked, this, &UserInfoPage::sig_logout);
}
UserInfoPage::~UserInfoPage() { delete ui; }
void UserInfoPage::Refresh()
{
  auto mgr = UserMgr::GetInstance();
  QPixmap image(mgr->GetIcon());
  if (image.isNull()) image.load(":/res/head_1.jpg");
  ui->head_lb->setPixmap(image.scaled(ui->head_lb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  ui->nick_ed->setText(mgr->GetNick());
  ui->name_ed->setText(mgr->GetName());
  ui->desc_ed->setText(mgr->GetDesc());
}
void UserInfoPage::on_up_btn_clicked()
{
  const int uid = UserMgr::GetInstance()->GetUid();
  if (uid <= 0) return;
  const auto file = QFileDialog::getOpenFileName(this, tr("选择头像"), {},
      tr("图片 (*.png *.jpg *.jpeg *.bmp *.webp)"));
  if (file.isEmpty()) return;
  const auto image = ImageCropperDialog::getCroppedImage(file, 600, 400, CropperShape::CIRCLE, QSize(), this);
  if (image.isNull() || UserMgr::GetInstance()->GetUid() != uid) return;
  QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
  if (!dir.mkpath("avatars")) {
    QMessageBox::warning(this, tr("保存失败"), tr("无法创建头像目录"));
    return;
  }
  const auto path = dir.filePath(QStringLiteral("avatars/%1.png").arg(uid));
  if (!image.save(path, "PNG")) {
    QMessageBox::warning(this, tr("保存失败"), tr("无法保存头像"));
    return;
  }
  UserMgr::GetInstance()->GetUserInfo()->_icon = path;
  Refresh();
  emit sig_avatar_changed();
}
