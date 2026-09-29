#include "userinfopage.h"
#include "ui_userinfopage.h"
#include "network/usermgr.h"
#include "network/tcpmgr.h"
#include "common/inputvalidator.h"
#include "widgets/imagecropperdialog.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QBuffer>
#include <QJsonDocument>
#include <QPainter>

UserInfoPage::UserInfoPage(QWidget *parent) : QWidget(parent), ui(new Ui::UserInfoPage)
{
  ui->setupUi(this);
  setAttribute(Qt::WA_StyledBackground);
  ui->name_ed->setMaxLength(64);
  ui->nick_ed->setMaxLength(64);
  ui->desc_ed->setMaxLength(255);
  ui->up_btn->setText(tr("选择头像"));
  ui->submit_btn->setText(tr("保存资料"));
  connect(ui->logout_btn, &QPushButton::clicked, this, &UserInfoPage::sig_logout);
  const auto tcp = TcpMgr::GetInstance();
  connect(tcp.get(), &TcpMgr::sig_profile_updated, this, [this](const QJsonObject &profile) {
    if (_savingUid <= 0 || profile["uid"].toInt() != _savingUid) return;
    auto mgr = UserMgr::GetInstance();
    auto user = mgr->GetUserInfo();
    if (!user || user->_uid != _savingUid) return;
    user->_name = profile["name"].toString();
    user->_nick = profile["nick"].toString();
    user->_desc = profile["desc"].toString();
    if (!_pendingAvatarBytes.isEmpty()) mgr->SetAvatar(_savingUid, _pendingAvatarBytes);
    _savingUid = 0;
    _pendingAvatar = QPixmap();
    _pendingAvatarBytes.clear();
    ui->submit_btn->setEnabled(true);
    ui->up_btn->setEnabled(true);
    ui->name_ed->setEnabled(true);
    ui->nick_ed->setEnabled(true);
    ui->desc_ed->setEnabled(true);
    Refresh(true);
    emit sig_profile_changed();
    if (profile["error"].toInt() == RedisError) {
      QMessageBox::warning(this, tr("资料已保存"),
          tr("资料已写入服务器，但缓存刷新失败；其他客户端可能暂时看到旧资料"));
    } else {
      QMessageBox::information(this, tr("保存成功"), tr("个人资料已保存到服务器"));
    }
  });
  connect(tcp.get(), &TcpMgr::sig_request_failed, this, [this](ReqId request, const QString &message) {
    if (request != ID_UPDATE_PROFILE_REQ || _savingUid <= 0) return;
    _savingUid = 0;
    ui->submit_btn->setEnabled(true);
    ui->up_btn->setEnabled(true);
    ui->name_ed->setEnabled(true);
    ui->nick_ed->setEnabled(true);
    ui->desc_ed->setEnabled(true);
    QMessageBox::warning(this, tr("保存失败"), message);
  });
  connect(UserMgr::GetInstance().get(), &UserMgr::sig_avatar_ready, this, [this](int uid) {
    if (uid != UserMgr::GetInstance()->GetUid() || !_pendingAvatar.isNull()) return;
    QPixmap image = UserMgr::GetInstance()->AvatarPixmap(uid, UserMgr::GetInstance()->GetIcon());
    ui->head_lb->setPixmap(image.scaled(ui->head_lb->size(), Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation));
  });
}
UserInfoPage::~UserInfoPage() { delete ui; }
void UserInfoPage::Refresh(bool force)
{
  auto mgr = UserMgr::GetInstance();
  if (_savingUid > 0 && _savingUid == mgr->GetUid()) return;
  if (!force && _displayedUid > 0 && _displayedUid == mgr->GetUid() &&
      (ui->name_ed->isModified() || ui->nick_ed->isModified() ||
       ui->desc_ed->isModified() || !_pendingAvatar.isNull())) return;
  _savingUid = 0;
  _displayedUid = mgr->GetUid();
  ui->submit_btn->setEnabled(true);
  ui->up_btn->setEnabled(true);
  ui->name_ed->setEnabled(true);
  ui->nick_ed->setEnabled(true);
  ui->desc_ed->setEnabled(true);
  _pendingAvatar = QPixmap();
  _pendingAvatarBytes.clear();
  QPixmap image = mgr->AvatarPixmap(mgr->GetUid(), mgr->GetIcon());
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
  _pendingAvatar = image;
  ui->head_lb->setPixmap(image.scaled(ui->head_lb->size(), Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
}

void UserInfoPage::on_submit_btn_clicked()
{
  if (_savingUid > 0) return;
  const auto mgr = UserMgr::GetInstance();
  const int uid = mgr->GetUid();
  if (uid <= 0 || !TcpMgr::GetInstance()->IsConnected()) {
    QMessageBox::warning(this, tr("保存失败"), tr("聊天连接已断开，请重新登录"));
    return;
  }
  const QString name = ui->name_ed->text().trimmed();
  const QString nick = ui->nick_ed->text().trimmed();
  const QString description = ui->desc_ed->text().trimmed();
  const QString nameError = name == mgr->GetName() ? QString() : InputValidator::userError(name);
  if (!nameError.isEmpty()) {
    QMessageBox::warning(this, tr("用户名无效"), nameError);
    ui->name_ed->setFocus();
    return;
  }
  if (nick.toUtf8().size() > 64) {
    QMessageBox::warning(this, tr("昵称无效"), tr("昵称不能超过64字节"));
    ui->nick_ed->setFocus();
    return;
  }
  if (description.toUtf8().size() > 255) {
    QMessageBox::warning(this, tr("描述过长"), tr("描述不能超过255字节"));
    ui->desc_ed->setFocus();
    return;
  }
  QJsonObject profile{{"name", name}, {"nick", nick}, {"desc", description}};
  _pendingAvatarBytes.clear();
  if (!_pendingAvatar.isNull()) {
    QImage thumb = _pendingAvatar.toImage().scaled(96, 96, Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation);
    QImage jpg(thumb.size(), QImage::Format_RGB32);
    jpg.fill(Qt::white);
    QPainter painter(&jpg);
    painter.drawImage(0, 0, thumb);
    painter.end();
    for (int quality : {80, 65, 50, 35}) {
      _pendingAvatarBytes.clear();
      QBuffer buffer(&_pendingAvatarBytes);
      buffer.open(QIODevice::WriteOnly);
      if (jpg.save(&buffer, "JPEG", quality) && _pendingAvatarBytes.size() <= 8192) break;
    }
    if (_pendingAvatarBytes.isEmpty() || _pendingAvatarBytes.size() > 8192) {
      QMessageBox::warning(this, tr("头像过大"), tr("无法将头像压缩到服务器允许的大小"));
      return;
    }
    profile["avatar"] = QString::fromLatin1(_pendingAvatarBytes.toBase64());
  }
  const QByteArray body = QJsonDocument(profile).toJson(QJsonDocument::Compact);
  if (body.size() > 16000) {
    QMessageBox::warning(this, tr("资料过大"), tr("资料内容超过服务器允许的大小"));
    return;
  }
  _savingUid = uid;
  ui->submit_btn->setEnabled(false);
  ui->up_btn->setEnabled(false);
  ui->name_ed->setEnabled(false);
  ui->nick_ed->setEnabled(false);
  ui->desc_ed->setEnabled(false);
  emit TcpMgr::GetInstance()->sig_send_data(ID_UPDATE_PROFILE_REQ, body);
}
