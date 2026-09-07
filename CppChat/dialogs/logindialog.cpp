#include "dialogs/logindialog.h"
#include "common/inputvalidator.h"
#include "network/httpmgr.h"
#include "network/tcpmgr.h"
#include "ui_logindialog.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
LoginDialog::LoginDialog(QWidget *parent) : QDialog(parent), ui(new Ui::LoginDialog)
{
  ui->setupUi(this);
  ui->forget_label->SetState("normal", "hover");
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
}

LoginDialog::~LoginDialog()
{
  delete ui;
}

void LoginDialog::initHttpHandlers()
{ // 注册获取登录回包逻辑
  _handlers.insert(ReqId::ID_LOGIN_USER, [this](QJsonObject jsonObj) {
    int error = jsonObj["error"].toInt();
    if (error != ErrorCodes::SUCCESS) {
      showTip(tr("参数错误"), false);
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
    qDebug() << "Chat endpoint received, uid:" << si.Uid;
    emit sig_connect_tcp(si);
  });
}
void LoginDialog::slot_tcp_con_finish(bool bsuccess)
{

  if (bsuccess) {
    showTip(tr("聊天服务连接成功，正在登录..."), true);
    QJsonObject jsonObj;
    jsonObj["uid"] = _uid;
    jsonObj["token"] = _token;

    QJsonDocument doc(jsonObj);
    QString jsonString = doc.toJson(QJsonDocument::Indented);

    // 发送tcp请求给chat server
    TcpMgr::GetInstance()->sig_send_data(ReqId::ID_CHAT_LOGIN, jsonString);

  } else {
    showTip(tr("网络异常"), false);
    enableBtn(true);
  }
}

void LoginDialog::slot_login_failed(int error)
{
  qWarning() << "TCP login failed, error:" << error;
  showTip(tr("登录失败，错误码：%1").arg(error), false);
  enableBtn(true);
}

void LoginDialog::initHead()
{
  // 加载图片
  QPixmap originalPixmap(":/res/head_1.jpg");
  // 设置图片自动缩放
  // qDebug() << originalPixmap.size() << ui->head_label->size();
  originalPixmap
    = originalPixmap.scaled(ui->head_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

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

bool LoginDialog::checkEmailValid()
{
  const QString message = InputValidator::emailError(ui->email_edit->text());
  if (!message.isEmpty()) {
    AddTipErr(TipError::TIP_EMAIL_ERR, message);
    return false;
  }
  DelTipErr(TipError::TIP_EMAIL_ERR);
  return true;
}

void LoginDialog::AddTipErr(TipError te, QString tips)
{
  _tipErrors[te] = tips;
  showTip(tips, false);
}

void LoginDialog::DelTipErr(TipError te)
{
  _tipErrors.remove(te);
  if (_tipErrors.empty()) {
    ui->err_tip->clear();
    return;
  }

  showTip(_tipErrors.first(), false);
}

void LoginDialog::showTip(QString str, bool b_ok)
{
  if (b_ok) {
    ui->err_tip->setProperty("state", "normal");
  } else {
    ui->err_tip->setProperty("state", "err");
  }

  ui->err_tip->setText(str);

  repolish(ui->err_tip);
}

bool LoginDialog::checkPwdValid()
{
  auto pwd = ui->pass_edit->text();
  if (pwd.length() < 6 || pwd.length() > 15) {
    qDebug() << "Pass length invalid";
    // 提示长度不准确
    AddTipErr(TipError::TIP_PWD_ERR, tr("密码长度应为6~15"));
    return false;
  }

  // 创建一个正则表达式对象，按照上述密码要求
  // 这个正则表达式解释：
  // ^[a-zA-Z0-9!@#$%^&*]{6,15}$ 密码长度至少6，可以是字母、数字和特定的特殊字符
  QRegularExpression regExp("^[a-zA-Z0-9!@#$%^&*]{6,15}$");
  bool match = regExp.match(pwd).hasMatch();
  if (!match) {
    // 提示字符非法
    AddTipErr(TipError::TIP_PWD_ERR, tr("不能包含非法字符且长度为(6~15)"));
    return false;
    ;
  }

  DelTipErr(TipError::TIP_PWD_ERR);

  return true;
}
bool LoginDialog::enableBtn(bool enabled)
{
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
  qDebug() << "Switch to password-reset dialog";
  emit switchReset();
}

void LoginDialog::on_login_btn_clicked()
{
  qDebug() << "login btn clicked";
  if (checkEmailValid() == false) {
    return;
  }

  if (checkPwdValid() == false) {
    return;
  }

  enableBtn(false);
  const auto email = ui->email_edit->text().trimmed();
  auto pwd = ui->pass_edit->text();
  // 发送http请求登录
  QJsonObject json_obj;
  json_obj["email"] = email;
  json_obj["passwd"] = pwd;
  HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix + "/user_login"), json_obj,
                                      ReqId::ID_LOGIN_USER, Modules::LOGINMOD);
}

void LoginDialog::slot_login_mod_finish(ReqId id, QString res, ErrorCodes err)
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

  // 调用对应的逻辑,根据id回调。
  _handlers[id](jsonDoc.object());
  showTip(tr("登录成功"), true);
  return;
}
