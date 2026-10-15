#include "dialogs/logindialog.h"
#include "common/autologinstore.h"
#include "common/formfeedback.h"
#include "common/inputvalidator.h"
#include "network/httpmgr.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "ui_logindialog.h"
#include <QCheckBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
LoginDialog::LoginDialog(QWidget *parent) : QDialog(parent), ui(new Ui::LoginDialog)
{
  ui->setupUi(this);
  ui->pass_edit->setEchoMode(QLineEdit::Password);
  _autoLogin = new QCheckBox(tr("下次自动登录"), this);
  _autoLogin->setObjectName("auto_login_check");
  _autoLogin->setChecked(AutoLoginStore::supported());
  _autoLogin->setEnabled(AutoLoginStore::supported());
  _autoLogin->setToolTip(tr("仅在本机当前 Windows 用户下加密保存登录信息；退出登录后清除"));
  ui->horizontalLayout_3->insertWidget(0, _autoLogin);
  _cancelAutoLogin = new QPushButton(tr("取消自动登录"), this);
  _cancelAutoLogin->setObjectName("cancel_auto_login_btn");
  _cancelAutoLogin->setAutoDefault(false);
  _cancelAutoLogin->hide();
  ui->horizontalLayout_3->insertWidget(1, _cancelAutoLogin);
  connect(_autoLogin, &QCheckBox::toggled, this, [this](bool enabled) {
    if (!enabled && !ForgetAutoLogin()) {
      const QSignalBlocker blocker(_autoLogin);
      _autoLogin->setChecked(true);
    }
  });
  connect(_cancelAutoLogin, &QPushButton::clicked, this, [this] {
    _pending = false;
    _automatic = false;
    _pendingPassword.clear();
    HttpMgr::GetInstance()->CancelRequest(ID_LOGIN_USER, Modules::LOGINMOD);
    TcpMgr::GetInstance()->CloseConnection();
    enableBtn(true);
    showTip(tr("已取消自动登录，可以手动登录"), false);
  });
  connect(ui->email_edit, &QLineEdit::returnPressed, ui->login_btn, &QPushButton::click);
  connect(ui->pass_edit, &QLineEdit::returnPressed, ui->login_btn, &QPushButton::click);
  _feedback = new FormFeedback(ui->err_tip, this);
  _feedback->addField(ui->email_edit,
                      [this]() { return InputValidator::emailError(ui->email_edit->text()); });
  _feedback->addField(ui->pass_edit,
                      [this]() { return InputValidator::passwordError(ui->pass_edit->text()); });
  ui->forget_label->SetState("normal", "hover", "hover", "normal", "hover", "hover");
  ui->forget_label->setCursor(Qt::PointingHandCursor);
  connect(ui->forget_label, &ClickedLabel::clicked, this, &LoginDialog::slot_forget_pwd);
  initHead();
  initHttpHandlers();
  // 连接登录回包信号
  connect(HttpMgr::GetInstance().get(), &HttpMgr::sig_login_mod_finish, this,
          &LoginDialog::slot_login_mod_finish);

  // 连接tcp连接请求的信号和槽函数
  connect(this, &LoginDialog::sig_connect_tcp, TcpMgr::GetInstance().get(),
          &TcpMgr::slot_tcp_connect);
  // 连接tcp管理者发出的连接成功信号
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_con_success, this,
          &LoginDialog::slot_tcp_con_finish);
  // 连接tcp管理者发出的登陆失败信号
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_login_failed, this,
          &LoginDialog::slot_login_failed);
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_swich_chatdlg, this, [this] {
    if (!_pending || UserMgr::GetInstance()->GetUid() != _uid)
      return;
    try {
      if (_autoLogin->isChecked())
        AutoLoginStore::save(QUrl(gate_url_prefix), {_pendingEmail, _pendingPassword});
      else
        AutoLoginStore::clear(QUrl(gate_url_prefix));
    } catch (...) {
      // Login succeeds even if local credential storage fails; never log the credentials.
      qWarning() << "Unable to persist secure automatic login";
      ForgetAutoLogin();
    }
    _pending = false;
    _automatic = false;
    _pendingPassword.clear();
    ui->pass_edit->clear();
    enableBtn(true);
  });
}

LoginDialog::~LoginDialog()
{
  if (_pending) {
    _pending = false;
    HttpMgr::GetInstance()->CancelRequest(ID_LOGIN_USER, Modules::LOGINMOD);
    TcpMgr::GetInstance()->CloseConnection();
  }
  delete ui;
}

void LoginDialog::initHttpHandlers()
{ // 注册获取登录回包逻辑
  _handlers.insert(ReqId::ID_LOGIN_USER, [this](QJsonObject jsonObj) {
    const int error = jsonObj.value(QStringLiteral("error")).toInt(ErrorCodes::Error_Json);
    if (error != ErrorCodes::Success) {
      if (error == ErrorCodes::PasswdInvalid || error == ErrorCodes::UidInvalid)
        ForgetAutoLogin();
      showTip(errorCodeMessage(error), false);
      enableBtn(true);
      return;
    }
    auto user = jsonObj["user"].toString();

    // 发送信号通知tcpMgr发送长链接
    ServerInfo si;
    si.Uid = jsonObj["uid"].toInt();
    si.Host = jsonObj["host"].toString();
    si.Port = jsonObj["port"].toString();
    si.Token = jsonObj["token"].toString();

    _uid = si.Uid;
    _token = si.Token;
    qDebug() << "Connecting chat server" << si.Host << si.Port;
    showTip(tr("账号验证成功，正在连接聊天服务..."), true);
    emit sig_connect_tcp(si);
  });
}
void LoginDialog::slot_tcp_con_finish(bool bsuccess)
{
  if (!_pending)
    return;
  if (bsuccess) {
    showTip(tr("聊天服务连接成功，正在登录..."), true);
    QJsonObject jsonObj;
    jsonObj["uid"] = _uid;
    jsonObj["token"] = _token;

    QJsonDocument doc(jsonObj);
    // QString jsonString = doc.toJson(QJsonDocument::Indented);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    // 发送tcp请求给chat server
    TcpMgr::GetInstance()->sig_send_data(ReqId::ID_CHAT_LOGIN, jsonData);

  } else {
    showTip(errorCodeMessage(ErrorCodes::NetworkError), false);
    enableBtn(true);
  }
}

void LoginDialog::slot_login_failed(int error)
{
  if (!_pending)
    return;
  qWarning() << "TCP login failed, error:" << error;
  showTip(errorCodeMessage(error), false);
  enableBtn(true);
}

void LoginDialog::initHead()
{
  // 加载图片
  QPixmap originalPixmap(":/res/head_1.jpg");
  // 设置图片自动缩放
  // qDebug() << originalPixmap.size() << ui->head_label->size();
  originalPixmap =
      originalPixmap.scaled(ui->head_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

  // 创建一个和原始图片相同大小的QPixmap，用于绘制圆角图片
  QPixmap roundedPixmap(originalPixmap.size());
  roundedPixmap.fill(Qt::transparent); // 用透明色填充

  QPainter painter(&roundedPixmap);
  painter.setRenderHint(QPainter::Antialiasing); // 设置抗锯齿，使圆角更平滑
  painter.setRenderHint(QPainter::SmoothPixmapTransform);

  // 使用QPainterPath设置圆角
  QPainterPath path;
  path.addRoundedRect(0, 0, originalPixmap.width(), originalPixmap.height(), 10,
                      10); // 最后两个参数分别是x和y方向的圆角半径
  painter.setClipPath(path);

  // 将原始图片绘制到roundedPixmap上
  painter.drawPixmap(0, 0, originalPixmap);

  // 设置绘制好的圆角图片到QLabel上
  ui->head_label->setPixmap(roundedPixmap);
}

void LoginDialog::showTip(QString str, bool b_ok) { _feedback->showStatus(str, b_ok); }
bool LoginDialog::enableBtn(bool enabled)
{
  if (enabled) {
    _pending = false;
    _automatic = false;
    _pendingPassword.clear();
  }
  ui->email_edit->setEnabled(enabled);
  ui->pass_edit->setEnabled(enabled);
  _autoLogin->setEnabled(enabled && AutoLoginStore::supported());
  _cancelAutoLogin->setVisible(!enabled && _automatic);
  ui->forget_label->setVisible(enabled || !_automatic);
  ui->forget_label->setEnabled(enabled);
  ui->login_btn->setEnabled(enabled);
  ui->reg_btn->setEnabled(enabled);
  return true;
}
void LoginDialog::on_reg_btn_clicked()
{
  qDebug() << "Switch to register dialog";
  emit switchRegister();
}
void LoginDialog::slot_forget_pwd()
{
  ForgetAutoLogin();
  qDebug() << "Switch to password-reset dialog";
  emit switchReset();
}

void LoginDialog::on_login_btn_clicked()
{
  if (_pending)
    return;
  qDebug() << "login btn clicked";
  if (!_feedback->validateAll()) {
    return;
  }

  _feedback->clearStatus();
  _pending = true;
  enableBtn(false);
  const auto email = ui->email_edit->text().trimmed();
  auto pwd = ui->pass_edit->text();
  _pendingEmail = email;
  _pendingPassword = pwd;
  // 发送http请求登录
  QJsonObject json_obj;
  json_obj["email"] = email;
  json_obj["passwd"] = pwd;
  HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix + "/user_login"), json_obj,
                                      ReqId::ID_LOGIN_USER, Modules::LOGINMOD);
}

void LoginDialog::slot_login_mod_finish(ReqId id, QByteArray data, ErrorCodes err)
{
  if (!_pending || id != ID_LOGIN_USER)
    return;
  if (err != ErrorCodes::Success) {
    showTip(errorCodeMessage(err), false);
    enableBtn(true);
    return;
  }

  // HTTP 回包保持为原始字节，可直接解析 JSON。
  QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
  // json解析错误
  if (jsonDoc.isNull()) {
    showTip(errorCodeMessage(ErrorCodes::Error_Json), false);
    enableBtn(true);
    return;
  }

  // json解析错误
  if (!jsonDoc.isObject()) {
    showTip(errorCodeMessage(ErrorCodes::Error_Json), false);
    enableBtn(true);
    return;
  }

  // 调用对应的逻辑,根据id回调。
  _handlers[id](jsonDoc.object());
  return;
}

void LoginDialog::ResetForLogin(const QString &message, bool forgetSaved)
{
  HttpMgr::GetInstance()->CancelRequest(ID_LOGIN_USER, Modules::LOGINMOD);
  const bool forgotten = !forgetSaved || ForgetAutoLogin();
  _uid = 0;
  _token.clear();
  ui->pass_edit->clear();
  _feedback->reset();
  enableBtn(true);
  showTip(forgotten ? message : tr("已退出，但无法清除自动登录信息，请检查数据目录权限"), false);
}

bool LoginDialog::ForgetAutoLogin()
{
  try {
    AutoLoginStore::clear(QUrl(gate_url_prefix));
    return true;
  } catch (...) {
    if (_feedback)
      showTip(tr("无法清除本机自动登录信息，请检查数据目录权限"), false);
    return false;
  }
}

void LoginDialog::TryAutoLogin()
{
  if (_autoAttempted || _pending || !AutoLoginStore::supported())
    return;
  _autoAttempted = true;
  try {
    const auto saved = AutoLoginStore::load(QUrl(gate_url_prefix));
    if (!saved)
      return;
    ui->email_edit->setText(saved->email);
    ui->pass_edit->setText(saved->password);
    _automatic = true;
    on_login_btn_clicked();
  } catch (...) {
    ForgetAutoLogin();
    showTip(tr("自动登录信息不可用，请手动登录"), false);
  }
}
