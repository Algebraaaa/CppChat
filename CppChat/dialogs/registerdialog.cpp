#include "dialogs/registerdialog.h"
#include "common/global.h"
#include "common/inputvalidator.h"
#include "network/httpmgr.h"
#include "ui_registerdialog.h"
#include <QJsonDocument>
#include <QJsonParseError>

RegisterDialog::RegisterDialog(QWidget *parent) : QDialog(parent), ui(new Ui::RegisterDialog)
{
  ui->setupUi(this);
  ui->pass_edit->setEchoMode(QLineEdit::Password);
  ui->confirm_edit->setEchoMode(QLineEdit::Password);

  // 初始化时设为绿色
  // QLabel一开始没有 state 这个属性，QSS 里 #err_tip[state='normal']
  // 匹配不上，颜色是默认的（黑色，继承自 #user_label, #pass_label 或系统默认）。
  ui->err_tip->setProperty("state", "normal");
  // err_tip 实际类型是 QLabel*
  repolish(ui->err_tip);
  ui->err_tip->clear();

  // 离开输入框时立即校验。即时校验只更新提示，不强行抢回焦点；
  // 点击注册时的校验仍会把焦点定位到第一个错误输入框。
  connect(ui->user_edit, &QLineEdit::editingFinished, this, [this]() { checkUserValid(); });
  connect(ui->email_edit, &QLineEdit::editingFinished, this, [this]() { checkEmailValid(); });
  connect(ui->pass_edit, &QLineEdit::editingFinished, this, [this]() {
    checkPasswordValid();
    if (!ui->confirm_edit->text().isEmpty()) {
      checkConfirmPasswordValid();
    }
  });
  connect(ui->confirm_edit, &QLineEdit::editingFinished, this,
          [this]() { checkConfirmPasswordValid(); });
  connect(ui->verify_edit, &QLineEdit::editingFinished, this, [this]() { checkVerifyCodeValid(); });

  connect(HttpMgr::GetInstance().get(), &HttpMgr::sig_reg_mod_finish, this,
          &RegisterDialog::slot_reg_mod_finish);
  initHttpHandlers();

  // ui->pass_visible->setCursor(Qt::PointingHandCursor);
  // ui->confirm_visible->setCursor(Qt::PointingHandCursor);
  ui->pass_visible->SetState("unvisible", "unvisible_hover", "visible_hover");
  ui->confirm_visible->SetState("unvisible", "unvisible_hover", "visible_hover");

  connect(ui->pass_visible, &ClickedLabel::pressed, this,
          [this]() { ui->pass_edit->setEchoMode(QLineEdit::Normal); });
  connect(ui->pass_visible, &ClickedLabel::released, this,
          [this]() { ui->pass_edit->setEchoMode(QLineEdit::Password); });
  connect(ui->confirm_visible, &ClickedLabel::pressed, this,
          [this]() { ui->confirm_edit->setEchoMode(QLineEdit::Normal); });
  connect(ui->confirm_visible, &ClickedLabel::released, this,
          [this]() { ui->confirm_edit->setEchoMode(QLineEdit::Password); });

  // 创建定时器
  _countdown_timer = new QTimer(this);
  // 连接信号和槽
  connect(_countdown_timer, &QTimer::timeout, this, [this]() {
    if (_countdown <= 1) {
      returnToLogin();
      return;
    }
    --_countdown;
    ui->tip_label->setText(tr("注册成功：%1s后返回登录").arg(_countdown));
  });

  resetRegistrationPage();
}

RegisterDialog::~RegisterDialog()
{
  delete ui;
}
void RegisterDialog::on_get_code_clicked()
{
  qDebug() << "Registration verification button clicked"
           << "email:" << ui->email_edit->text().trimmed();
  if (checkEmailValid(true)) {
    const QString email = ui->email_edit->text().trimmed();
    // showTip(tr("验证码已发送"), true);
    QJsonObject json_obj;
    json_obj["email"] = email;

    // 接下来客户端调用 HttpMgr
    HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix + "/get_verifycode"), json_obj,
                                        ReqId::ID_GET_VERIFY_CODE, Modules::REGISTERMOD);
    ui->get_code->startCountdown();
    // 发送验证码
  }
}

void RegisterDialog::showTip(QString str, bool b_ok)
{
  if (b_ok) {
    ui->err_tip->setProperty("state", "normal");
  } else {
    ui->err_tip->setProperty("state", "err");
  }
  ui->err_tip->setText(str);

  repolish(ui->err_tip);
}

void RegisterDialog::addTipError(TipError error, const QString &message)
{
  _tipErrors[error] = message;
  showTip(message, false);
}

void RegisterDialog::removeTipError(TipError error)
{
  // 没有缓存过这个字段的错误时，不清除验证码发送结果等其他提示。
  if (_tipErrors.remove(error) == 0) {
    return;
  }

  if (_tipErrors.isEmpty()) {
    ui->err_tip->clear();
    return;
  }

  // QMap 按 TipError 的枚举顺序显示当前优先级最高的剩余错误。
  showTip(_tipErrors.first(), false);
}

bool RegisterDialog::checkUserValid(bool focusOnError)
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

bool RegisterDialog::checkEmailValid(bool focusOnError)
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

bool RegisterDialog::checkPasswordValid(bool focusOnError)
{
  const QString message = InputValidator::passwordError(ui->pass_edit->text());

  if (!message.isEmpty()) {
    addTipError(TipError::Password, message);
    if (focusOnError) {
      ui->pass_edit->setFocus();
    }
    return false;
  }

  removeTipError(TipError::Password);
  return true;
}

bool RegisterDialog::checkConfirmPasswordValid(bool focusOnError)
{
  const QString message
    = InputValidator::confirmPasswordError(ui->pass_edit->text(), ui->confirm_edit->text());

  if (!message.isEmpty()) {
    addTipError(TipError::ConfirmPassword, message);
    if (focusOnError) {
      ui->confirm_edit->setFocus();
    }
    return false;
  }

  removeTipError(TipError::ConfirmPassword);
  return true;
}

bool RegisterDialog::checkVerifyCodeValid(bool focusOnError)
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

void RegisterDialog::on_cancel_btn_clicked()
{
  returnToLogin();
}

void RegisterDialog::slot_reg_mod_finish(ReqId id, QString res, ErrorCodes err)
{
  if (err != ErrorCodes::SUCCESS) {
    if (id == ReqId::ID_GET_VERIFY_CODE) {
      ui->get_code->resetCountdown();
    }
    showTip(tr("网络请求错误"), false);
    return;
  }

  // 解析JSON 字符串，res转化为QByteArray
  QJsonParseError parseError;
  const QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !jsonDoc.isObject()) {
    if (id == ReqId::ID_GET_VERIFY_CODE) {
      ui->get_code->resetCountdown();
    }
    showTip(tr("json解析失败"), false);
    return;
  }

  const auto handler = _handlers.constFind(id);
  if (handler == _handlers.cend()) {
    showTip(tr("未找到对应的响应处理逻辑"), false);
    return;
  }
  (*handler)(jsonDoc.object());
}

void RegisterDialog::initHttpHandlers()
{
  // 注册获取验证码回报的逻辑
  _handlers.insert(ReqId::ID_GET_VERIFY_CODE, [this](const QJsonObject &jsonObj) {
    int error = jsonObj["error"].toInt();
    if (error != ErrorCodes::SUCCESS) {
      qWarning() << "Registration verification request rejected"
                 << "error:" << error;
      ui->get_code->resetCountdown();
      switch (error) {
      case 1:
        showTip(tr("验证码保存失败，请稍后重试"), false);
        break;
      case 2:
        showTip(tr("邮件发送失败，请检查邮件服务配置"), false);
        break;
      case 1001:
        showTip(tr("请求数据格式错误"), false);
        break;
      case 1002:
        showTip(tr("验证码服务暂时不可用"), false);
        break;
      default:
        showTip(tr("获取验证码失败，错误码：%1").arg(error), false);
        break;
      }
      return;
    }
    showTip(tr("验证码已经发送到邮箱"), true);
    qInfo() << "Registration verification code request succeeded";
    qDebug() << "Registration verification email:" << jsonObj["email"].toString();
  });

  // 注册注册用户回包逻辑
  _handlers.insert(ReqId::ID_REG_USER, [this](const QJsonObject &jsonObj) {
    int error = jsonObj["error"].toInt();
    if (error != ErrorCodes::SUCCESS) {
      qWarning() << "User registration request rejected"
                 << "error:" << error;
      showTip(tr("参数错误"), false);
      return;
    }
    showTip(tr("用户注册成功"), true);
    qInfo() << "User registration succeeded";
    // 测试阶段保留服务端返回的关键标识；协议字段当前统一为 uid。
    qDebug() << "Registered user"
             << "email:" << jsonObj["email"].toString()
             << "uid:" << jsonObj["uid"].toString();
    ChangeTipPage();
  });
}
void RegisterDialog::ChangeTipPage()
{
  _countdown_timer->stop();
  _countdown = 5;
  ui->stackedWidget->setCurrentWidget(ui->page_2);
  ui->tip_label->setText(tr("注册成功：%1s后返回登录").arg(_countdown));

  // 启动定时器，设置间隔为1000毫秒（1秒）
  _countdown_timer->start(1000);
}
void RegisterDialog::on_confirm_btn_clicked()
{
  qDebug() << "Registration confirm clicked"
           << "user:" << ui->user_edit->text().trimmed()
           << "email:" << ui->email_edit->text().trimmed();
  if (!checkUserValid(true) || !checkEmailValid(true) || !checkPasswordValid(true)
      || !checkConfirmPasswordValid(true) || !checkVerifyCodeValid(true)) {
    return;
  }

  const QString user = ui->user_edit->text().trimmed();
  const QString email = ui->email_edit->text().trimmed();
  const QString password = ui->pass_edit->text();
  const QString confirmPassword = ui->confirm_edit->text();
  const QString verifyCode = ui->verify_edit->text().trimmed();

  QJsonObject json_obj;
  json_obj["user"] = user;
  json_obj["email"] = email;
  json_obj["passwd"] = password;
  json_obj["confirm"] = confirmPassword;
  json_obj["verifycode"] = verifyCode;
  HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix + "/user_register"), json_obj,
                                      ReqId::ID_REG_USER, Modules::REGISTERMOD);
}

void RegisterDialog::resetRegistrationPage()
{
  if (_countdown_timer) {
    _countdown_timer->stop();
  }
  _countdown = 5;
  ui->stackedWidget->setCurrentWidget(ui->page);
  ui->tip_label->setText(tr("注册成功：%1s后返回登录").arg(_countdown));

  ui->user_edit->clear();
  ui->email_edit->clear();
  ui->pass_edit->clear();
  ui->confirm_edit->clear();
  ui->verify_edit->clear();
  ui->pass_edit->setEchoMode(QLineEdit::Password);
  ui->confirm_edit->setEchoMode(QLineEdit::Password);
  ui->pass_visible->SetState("unvisible", "unvisible_hover", "visible_hover");
  ui->confirm_visible->SetState("unvisible", "unvisible_hover", "visible_hover");

  _tipErrors.clear();
  ui->err_tip->clear();
  ui->get_code->resetCountdown();
}

void RegisterDialog::returnToLogin()
{
  resetRegistrationPage();
  emit switchLogin();
}

void RegisterDialog::on_return_btn_clicked()
{
  returnToLogin();
}
