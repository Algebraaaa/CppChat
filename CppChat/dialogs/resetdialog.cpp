#include "dialogs/resetdialog.h"
#include "common/inputvalidator.h"
#include "network/httpmgr.h"
#include "ui_resetdialog.h"
#include <QJsonDocument>
#include <QJsonObject>

ResetDialog::ResetDialog(QWidget *parent) : QDialog(parent), ui(new Ui::ResetDialog)
{
  ui->setupUi(this);
  ui->newpsd_edit->setEchoMode(QLineEdit::Password);
  ui->error_label->setProperty("state", "normal");
  repolish(ui->error_label);
  ui->error_label->clear();

  // 和注册页保持一致：离开输入框时立即校验，但不抢回焦点。
  connect(ui->user_edit, &QLineEdit::editingFinished, this, [this]() { checkUserValid(); });
  connect(ui->email_edit, &QLineEdit::editingFinished, this, [this]() { checkEmailValid(); });
  connect(ui->verify_edit, &QLineEdit::editingFinished, this, [this]() { checkVerifyCodeValid(); });
  connect(ui->newpsd_edit, &QLineEdit::editingFinished, this, [this]() { checkPasswordValid(); });
  connect(HttpMgr::GetInstance().get(), &HttpMgr::sig_reset_mod_finish, this,
          &ResetDialog::slot_reset_mod_finish);

  initHttpHandlers();
}

ResetDialog::~ResetDialog()
{
  delete ui;
}

void ResetDialog::initHttpHandlers()
{
  // 注册获取验证码回包逻辑
  _handlers.insert(ReqId::ID_GET_VERIFY_CODE, [this](QJsonObject jsonObj) {
    int error = jsonObj["error"].toInt();
    if (error != ErrorCodes::SUCCESS) {
      qWarning() << "Password-reset verification request rejected"
                 << "error:" << error;
      showTip(tr("参数错误"), false);
      return;
    }
    showTip(tr("验证码已发送到邮箱，注意查收"), true);
    qInfo() << "Password-reset verification code request succeeded";
    qDebug() << "Password-reset verification email accepted.";
  });

  // 注册注册用户回包逻辑
  _handlers.insert(ReqId::ID_RESET_PWD, [this](QJsonObject jsonObj) {
    int error = jsonObj["error"].toInt();
    if (error != ErrorCodes::SUCCESS) {
      qWarning() << "Password reset request rejected"
                 << "error:" << error;
      showTip(tr("参数错误"), false);
      return;
    }
    showTip(tr("重置成功,点击返回登录"), true);
    qInfo() << "Password reset succeeded";
    qDebug() << "Password reset user"
             << "email:" << jsonObj["email"].toString()
             << "uid:" << jsonObj["uid"].toString();
  });
}
void ResetDialog::on_cancel_btn_clicked()
{
  resetPage();
  emit switchLogin();
}

void ResetDialog::on_confirm_btn_clicked()
{
  qDebug() << "Password-reset confirm clicked";
  if (!checkUserValid(true) || !checkEmailValid(true) || !checkVerifyCodeValid(true)
      || !checkPasswordValid(true)) {
    return;
  }

  // 当前先完成前端输入检查；重置密码接口接入后在这里发送请求。
  showTip(tr("输入检查通过"), true);
  QJsonObject json_obj;
  json_obj["user"] = ui->user_edit->text();
  json_obj["email"] = ui->email_edit->text();
  json_obj["passwd"] = ui->newpsd_edit->text();
  json_obj["verifycode"] = ui->verify_edit->text();
  HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix + "/reset_pwd"), json_obj,
                                      ReqId::ID_RESET_PWD, Modules::RESETMOD);
}

void ResetDialog::on_get_verify_btn_clicked()
{
  const QString email = ui->email_edit->text().trimmed();
  qDebug() << "Password-reset verification button clicked"
           << "email:" << email;
  if (!checkEmailValid(true)) {
    return;
  }

  // 发送http请求获取验证码
  QJsonObject json_obj;
  json_obj["email"] = email;
  HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix + "/get_verifycode"), json_obj,
                                      ReqId::ID_GET_VERIFY_CODE, Modules::RESETMOD);
}

void ResetDialog::slot_reset_mod_finish(ReqId id, QString res, ErrorCodes err)
{
  if (err != ErrorCodes::SUCCESS) {
    showTip(tr("网络请求错误"), false);
    return;
  }

  // 解析 JSON 字符串,res需转化为QByteArray
  QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
  // json解析错误
  if (jsonDoc.isNull()) {
    showTip(tr("json解析错误"), false);
    return;
  }

  // json解析错误
  if (!jsonDoc.isObject()) {
    showTip(tr("json解析错误"), false);
    return;
  }

  // 只查找已经注册的处理函数，避免 operator[] 为未知 id 插入空函数。
  const auto handler = _handlers.constFind(id);
  if (handler == _handlers.cend()) {
    showTip(tr("未找到对应的响应处理逻辑"), false);
    return;
  }

  const auto &handlerFunction = handler.value();
  handlerFunction(jsonDoc.object());
}

void ResetDialog::showTip(const QString &message, bool ok)
{
  ui->error_label->setProperty("state", ok ? "normal" : "err");
  ui->error_label->setText(message);
  repolish(ui->error_label);
}

void ResetDialog::addTipError(TipError error, const QString &message)
{
  _tipErrors[error] = message;
  showTip(message, false);
}

void ResetDialog::removeTipError(TipError error)
{
  if (_tipErrors.remove(error) == 0) {
    return;
  }

  if (_tipErrors.isEmpty()) {
    ui->error_label->clear();
    return;
  }

  // ResetDialog 的界面顺序是：用户名、邮箱、验证码、新密码。
  // 它和 RegisterDialog 的字段顺序不同，因此这里按本界面顺序找下一条错误。
  const TipError priorities[]
    = { TipError::User, TipError::Email, TipError::VerifyCode, TipError::NewPassword };
  for (TipError priority : priorities) {
    const auto error = _tipErrors.constFind(priority);
    if (error != _tipErrors.cend()) {
      showTip(error.value(), false);
      return;
    }
  }
}

bool ResetDialog::checkUserValid(bool focusOnError)
{
  const QString message = InputValidator::userError(ui->user_edit->text());
  if (!message.isEmpty()) {
    addTipError(TipError::User, message);
    if (focusOnError) {
      ui->user_edit->setFocus();
    }
    return false;
  }

  removeTipError(TipError::User);
  return true;
}

bool ResetDialog::checkEmailValid(bool focusOnError)
{
  const QString message = InputValidator::emailError(ui->email_edit->text());
  if (!message.isEmpty()) {
    addTipError(TipError::Email, message);
    if (focusOnError) {
      ui->email_edit->setFocus();
    }
    return false;
  }

  removeTipError(TipError::Email);
  return true;
}

bool ResetDialog::checkPasswordValid(bool focusOnError)
{
  const QString message = InputValidator::passwordError(ui->newpsd_edit->text());
  if (!message.isEmpty()) {
    addTipError(TipError::NewPassword, message);
    if (focusOnError) {
      ui->newpsd_edit->setFocus();
    }
    return false;
  }

  removeTipError(TipError::NewPassword);
  return true;
}

bool ResetDialog::checkVerifyCodeValid(bool focusOnError)
{
  const QString message = InputValidator::verifyCodeError(ui->verify_edit->text());
  if (!message.isEmpty()) {
    addTipError(TipError::VerifyCode, message);
    if (focusOnError) {
      ui->verify_edit->setFocus();
    }
    return false;
  }

  removeTipError(TipError::VerifyCode);
  return true;
}

void ResetDialog::resetPage()
{
  ui->user_edit->clear();
  ui->email_edit->clear();
  ui->verify_edit->clear();
  ui->newpsd_edit->clear();
  ui->newpsd_edit->setEchoMode(QLineEdit::Password);
  _tipErrors.clear();
  ui->error_label->setProperty("state", "normal");
  ui->error_label->clear();
  repolish(ui->error_label);
}
