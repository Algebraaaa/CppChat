#include "appbridge.h"
#include "common/chatstore.h"
#include "common/inputvalidator.h"
#include "common/logger.h"
#include "common/runtimepaths.h"
#include "network/httpmgr.h"
#include "network/resourcemgr.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/chatsyncservice.h"
#include "services/messagesender.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>

namespace {
QString firstError(std::initializer_list<QString> errors)
{
  for (const auto &error : errors)
    if (!error.isEmpty())
      return error;
  return {};
}
} // namespace

AppBridge::AppBridge(QObject *parent) : QObject(parent) {}

AppBridge::~AppBridge()
{
  resetSession();
  ChatStore::instance()->Shutdown();
}

bool AppBridge::init(const QString &dataDirectory)
{
  if (m_initialized)
    return true;
  m_dataDirectory = dataDirectory;
  loadConfig(); // RuntimePaths must be configured before any singleton is constructed.
  if (!Logger::install())
    qWarning() << "Failed to initialize file logger";
  m_sync = new ChatSyncService(this);
  m_sender = new MessageSender(this);
  initializeMedia();
  connectBackends();
  initializeFeatures();
  initializeRecovery();
  m_initialized = true;
  return true;
}

void AppBridge::loadConfig()
{
  const auto directory = m_dataDirectory.isEmpty()
                             ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                             : m_dataDirectory;
  m_configPath = QDir(directory).filePath(QStringLiteral("config.ini"));
  if (!QFile::exists(m_configPath)) {
    if (!QDir().mkpath(directory) || !QFile::copy(":/android/config.ini", m_configPath))
      qWarning() << "Cannot create client configuration";
    else
      QFile::setPermissions(m_configPath, QFile::ReadOwner | QFile::WriteOwner);
  }
  RuntimePaths::configure(m_configPath, directory, directory + "/logs");
  QSettings settings(m_configPath, QSettings::IniFormat);
  m_gateUrl = settings.value("GateServer/url").toString().trimmed();
  if (m_gateUrl.isEmpty()) {
    const auto host = settings.value("GateServer/host").toString().trimmed();
    if (!host.isEmpty()) {
      QUrl url;
      url.setScheme(settings.value("GateServer/scheme", "https").toString());
      url.setHost(host);
      url.setPort(settings.value("GateServer/port", 443).toInt());
      m_gateUrl = url.toString(QUrl::StripTrailingSlash);
    }
  }
  gate_url_prefix = m_gateUrl;
}

bool AppBridge::beginAuth(ReqId request)
{
  if (authBusy() || isLoggedIn())
    return false;
  if (m_gateUrl.isEmpty()) {
    const auto message = tr("服务暂不可用，请稍后重试");
    if (request == ID_LOGIN_USER)
      emit loginHttpResult(NetworkError, message);
    else if (request == ID_REG_USER)
      emit registerResult(NetworkError, message);
    else if (request == ID_RESET_PWD)
      emit resetResult(NetworkError, message);
    else
      emit verifyCodeResult(NetworkError, message);
    return false;
  }
  m_authRequest = request;
  emit authBusyChanged();
  return true;
}

void AppBridge::finishAuth()
{
  m_authRequest = 0;
  m_loginPassword.clear();
  m_loginServer = {};
  emit authBusyChanged();
}

void AppBridge::connectBackends()
{
  const auto http = HttpMgr::GetInstance();
  connect(http.get(), &HttpMgr::sig_reg_mod_finish, this, &AppBridge::handleHttp);
  connect(http.get(), &HttpMgr::sig_reset_mod_finish, this, &AppBridge::handleHttp);
  connect(http.get(), &HttpMgr::sig_login_mod_finish, this, &AppBridge::handleHttp);
  connectChatBackends();
}

void AppBridge::handleHttp(ReqId id, const QByteArray &bytes, ErrorCodes transportError)
{
  if (m_authRequest != id)
    return;
  const auto document = QJsonDocument::fromJson(bytes);
  const auto object = document.object();
  int error = transportError;
  if (error == Success)
    error = document.isObject() ? object.value("error").toInt(Error_Json) : Error_Json;
  if (id == ID_LOGIN_USER && error == Success) {
    m_loginServer.Uid = object.value("uid").toInt();
    m_loginServer.Host = object.value("host").toString().trimmed();
    m_loginServer.Port = object.value("port").toVariant().toString();
    m_loginServer.Token = object.value("token").toString();
    bool validPort = false;
    const int port = m_loginServer.Port.toInt(&validPort);
    if (m_loginServer.Uid > 0 && !m_loginServer.Host.isEmpty() && !m_loginServer.Token.isEmpty() &&
        validPort && port > 0 && port <= 65535) {
      TcpMgr::GetInstance()->slot_tcp_connect(m_loginServer);
      return; // Keep busy until TCP authentication and account storage are ready.
    }
    error = Error_Json;
  }
  if (id == ID_LOGIN_USER && error != Success) {
    loginFailed(error, errorCodeMessage(error));
    return;
  }
  finishAuth();
  const auto message = errorCodeMessage(error);
  switch (id) {
  case ID_GET_VERIFY_CODE:
    emit verifyCodeResult(error, error == Success ? tr("验证码已发送，请查收邮箱") : message);
    break;
  case ID_REG_USER:
    emit registerResult(error, message);
    break;
  case ID_RESET_PWD:
    emit resetResult(error, message);
    break;
  default:
    emit loginHttpResult(error, message);
    break;
  }
}

void AppBridge::requestVerifyCode(const QString &email)
{
  const auto error = InputValidator::emailError(email);
  if (!error.isEmpty()) {
    emit verifyCodeResult(Error_Json, error);
    return;
  }
  if (beginAuth(ID_GET_VERIFY_CODE))
    HttpMgr::GetInstance()->PostHttpReq(QUrl(m_gateUrl + "/get_verifycode"),
                                        {{"email", email.trimmed()}}, ID_GET_VERIFY_CODE,
                                        REGISTERMOD);
}

void AppBridge::registerUser(const QString &email, const QString &password, const QString &confirm,
                             const QString &code, const QString &name)
{
  const auto error = firstError({InputValidator::userError(name), InputValidator::emailError(email),
                                 InputValidator::passwordError(password),
                                 InputValidator::confirmPasswordError(password, confirm),
                                 InputValidator::verifyCodeError(code)});
  if (!error.isEmpty()) {
    emit registerResult(Error_Json, error);
    return;
  }
  if (beginAuth(ID_REG_USER))
    HttpMgr::GetInstance()->PostHttpReq(QUrl(m_gateUrl + "/user_register"),
                                        {{"email", email.trimmed()},
                                         {"passwd", password},
                                         {"confirm", confirm},
                                         {"verifycode", code.trimmed()},
                                         {"user", name.trimmed()}},
                                        ID_REG_USER, REGISTERMOD);
}

void AppBridge::resetPassword(const QString &email, const QString &password, const QString &confirm,
                              const QString &code, const QString &name)
{
  const auto error = firstError({InputValidator::userError(name), InputValidator::emailError(email),
                                 InputValidator::passwordError(password),
                                 InputValidator::confirmPasswordError(password, confirm),
                                 InputValidator::verifyCodeError(code)});
  if (!error.isEmpty()) {
    emit resetResult(Error_Json, error);
    return;
  }
  if (beginAuth(ID_RESET_PWD))
    HttpMgr::GetInstance()->PostHttpReq(QUrl(m_gateUrl + "/reset_pwd"),
                                        {{"email", email.trimmed()},
                                         {"passwd", password},
                                         {"verifycode", code.trimmed()},
                                         {"user", name.trimmed()}},
                                        ID_RESET_PWD, RESETMOD);
}

void AppBridge::loginUser(const QString &email, const QString &password)
{
  const auto error =
      firstError({InputValidator::emailError(email), InputValidator::passwordError(password)});
  if (!error.isEmpty()) {
    emit loginHttpResult(Error_Json, error);
    return;
  }
  if (authBusy() || isLoggedIn() || sessionActive())
    return;
  m_session.prepare(email.trimmed(), password);
  startLogin(email, password);
}

void AppBridge::startLogin(const QString &email, const QString &password)
{
  if (beginAuth(ID_LOGIN_USER)) {
    m_loginEmail = email.trimmed();
    m_loginPassword = password;
    HttpMgr::GetInstance()->PostHttpReq(QUrl(m_gateUrl + "/user_login"),
                                        {{"email", email.trimmed()}, {"passwd", password}},
                                        ID_LOGIN_USER, LOGINMOD);
  }
}

bool AppBridge::isLoggedIn() const
{
  return TcpMgr::GetInstance()->IsConnected();
}
bool AppBridge::storeReady() const
{
  return m_sync && m_sync->IsReady();
}
bool AppBridge::sending() const
{
  return m_pendingImage || (m_sender && m_sender->SavingMessages());
}
int AppBridge::currentUid() const
{
  return UserMgr::GetInstance()->GetUid();
}
QString AppBridge::currentUser() const
{
  return UserMgr::GetInstance()->GetName();
}
QString AppBridge::currentNick() const
{
  return UserMgr::GetInstance()->GetNick();
}
QString AppBridge::currentDescription() const
{
  return UserMgr::GetInstance()->GetDesc();
}
QString AppBridge::errorCodeMessage(int code) const
{
  return ::errorCodeMessage(code);
}
