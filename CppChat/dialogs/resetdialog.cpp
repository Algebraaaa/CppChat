#include "dialogs/resetdialog.h"
#include "common/formfeedback.h"
#include "common/inputvalidator.h"
#include "network/httpmgr.h"
#include "ui_resetdialog.h"
#include <QJsonDocument>
#include <QJsonObject>

ResetDialog::ResetDialog(QWidget *parent) : QDialog(parent), ui(new Ui::ResetDialog)
{
  ui->setupUi(this);
  ui->newpsd_edit->setEchoMode(QLineEdit::Password);
  _feedback = new FormFeedback(ui->error_label, this);
  _feedback->addField(ui->user_edit, [this]() {
    return InputValidator::userError(ui->user_edit->text());
  });
  _feedback->addField(ui->email_edit, [this]() {
    return InputValidator::emailError(ui->email_edit->text());
  });
  _feedback->addField(ui->verify_edit, [this]() {
    return InputValidator::verifyCodeError(ui->verify_edit->text());
  });
  _feedback->addField(ui->newpsd_edit, [this]() {
    return InputValidator::passwordError(ui->newpsd_edit->text());
  });
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
    const int error = jsonObj.value(QStringLiteral("error")).toInt(ErrorCodes::Error_Json);
    if (error != ErrorCodes::Success) {
      qWarning() << "Password-reset verification request rejected"
                 << "error:" << error;
      showTip(errorCodeMessage(error), false, ReqId::ID_GET_VERIFY_CODE);
      return;
    }
    showTip(tr("验证码已发送到邮箱，注意查收"), true, ReqId::ID_GET_VERIFY_CODE);
    qInfo() << "Password-reset verification code request succeeded";
    qDebug() << "Password-reset verification email:" << jsonObj["email"].toString();
  });

  // 注册注册用户回包逻辑
  _handlers.insert(ReqId::ID_RESET_PWD, [this](QJsonObject jsonObj) {
    const int error = jsonObj.value(QStringLiteral("error")).toInt(ErrorCodes::Error_Json);
    if (error != ErrorCodes::Success) {
      qWarning() << "Password reset request rejected"
                 << "error:" << error;
      showTip(errorCodeMessage(error), false);
      return;
    }
    showTip(tr("重置成功,点击返回登录"), true);
    qInfo() << "Password reset succeeded";
    qDebug() << "Password reset user"
             << "email:" << jsonObj["email"].toString() << "uid:" << jsonObj["uid"].toString();
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
  if (!_feedback->validateAll()) {
    return;
  }

  _feedback->clearStatus();
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
  if (!_feedback->validateField(ui->email_edit, true)) {
    return;
  }

  _feedback->clearStatus();
  // 发送http请求获取验证码
  QJsonObject json_obj;
  json_obj["email"] = email;
  HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix + "/get_verifycode"), json_obj,
                                      ReqId::ID_GET_VERIFY_CODE, Modules::RESETMOD);
}

void ResetDialog::slot_reset_mod_finish(ReqId id, QByteArray data, ErrorCodes err)
{
  if (err != ErrorCodes::Success) {
    showTip(errorCodeMessage(err), false, id);
    return;
  }

  // HTTP 回包保持为原始字节，可直接解析 JSON。
  QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
  // json解析错误
  if (jsonDoc.isNull()) {
    showTip(errorCodeMessage(ErrorCodes::Error_Json), false, id);
    return;
  }

  // json解析错误
  if (!jsonDoc.isObject()) {
    showTip(errorCodeMessage(ErrorCodes::Error_Json), false, id);
    return;
  }

  // 只查找已经注册的处理函数，避免 operator[] 为未知 id 插入空函数。
  const auto handler = _handlers.constFind(id);
  if (handler == _handlers.cend()) {
    showTip(tr("未找到对应的响应处理逻辑"), false, id);
    return;
  }

  const auto &handlerFunction = handler.value();
  handlerFunction(jsonDoc.object());
}

void ResetDialog::showTip(const QString &message, bool ok, ReqId id)
{
  if (id == ReqId::ID_GET_VERIFY_CODE) {
    _feedback->showStatus(message, ok, {ui->email_edit});
  } else {
    _feedback->showStatus(message, ok);
  }
}

void ResetDialog::resetPage()
{
  ui->user_edit->clear();
  ui->email_edit->clear();
  ui->verify_edit->clear();
  ui->newpsd_edit->clear();
  ui->newpsd_edit->setEchoMode(QLineEdit::Password);
  _feedback->reset();
}
