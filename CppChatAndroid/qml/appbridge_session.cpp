#include "appbridge.h"
#include "network/httpmgr.h"
#include "network/resourcemgr.h"
#include "network/tcpmgr.h"
#include "services/messagesender.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QNetworkInformation>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

bool AppBridge::backgroundApplication()
{
#ifdef Q_OS_ANDROID
  if (sessionActive() && QNativeInterface::QAndroidApplication::isActivityContext()) {
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
      const auto activity = QNativeInterface::QAndroidApplication::context();
      if (activity.isValid())
        activity.callMethod<jboolean>("moveTaskToBack", jboolean(true));
      return QVariant();
    });
    return true;
  }
#endif
  return false;
}

void AppBridge::initializeRecovery()
{
  connect(&m_session, &MobileSession::changed, this, &AppBridge::loginStatusChanged);
  connect(&m_session, &MobileSession::reconnectRequested, this, [this] {
    if (!authBusy())
      startLogin(m_session.email(), m_session.password());
    else
      m_session.retryFailed();
  });
  QNetworkInformation::loadDefaultBackend();
  if (auto network = QNetworkInformation::instance()) {
    const auto available = [this](QNetworkInformation::Reachability state) {
      m_session.setAvailable(state != QNetworkInformation::Reachability::Disconnected);
    };
    available(network->reachability());
    connect(network, &QNetworkInformation::reachabilityChanged, this, available);
    connect(network, &QNetworkInformation::transportMediumChanged, this,
            [this](QNetworkInformation::TransportMedium) { reconnectNow(); });
  }
  if (auto app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
    auto background = std::make_shared<QElapsedTimer>();
    connect(app, &QGuiApplication::applicationStateChanged, this,
            [this, background](Qt::ApplicationState state) {
              if (state != Qt::ApplicationActive) {
                if (!background->isValid())
                  background->start();
                return;
              }
              // A socket surviving Android suspension may be half-open. Renew
              // authentication instead of trusting its ConnectedState alone.
              if (background->isValid() && background->elapsed() >= 25000 && sessionActive())
                reconnectNow();
              else
                m_session.retryNow();
              background->invalidate();
            });
  }
}
QString AppBridge::connectionText() const
{
  if (reconnecting())
    return tr("连接已中断，正在自动重连；本地记录已保留");
  return isLoggedIn() ? tr("已连接") : tr("尚未连接");
}
void AppBridge::reconnectNow()
{
  if (sessionActive() && !reconnecting() && !authBusy()) {
    TcpMgr::GetInstance()->CloseConnection();
    connectionInterrupted();
  }
  m_session.retryNow();
}
void AppBridge::connectionInterrupted()
{
  if (m_media)
    m_media->reset();
  m_requestedFiles.clear();
  m_savingVoice.clear();
  if (m_session.active() || m_session.recovering()) {
    // Keep the current account database, pages and draft available offline.
    // In-flight sends remain durable and can be retried after synchronization.
    m_sender->Reset();
    ResourceMgr::instance()->ResetSession();
    m_pendingImage.reset();
    m_imageThread.reset();
    m_pendingText.clear();
    m_pendingTextThread = 0;
    emit sendingChanged();
    if (m_session.recovering() && authBusy())
      loginFailed(NetworkError, tr("连接中断，正在重试"));
    else
      m_session.interrupted();
    emit loginStatusChanged();
    return;
  }
  if (authBusy())
    loginFailed(NetworkError, tr("连接失败，请稍后重试"));
}
void AppBridge::loginFailed(int error, const QString &message)
{
  if (m_authRequest != ID_LOGIN_USER)
    return;
  const bool rejected =
      error == PasswdErr || error == PasswdInvalid || error == EmailNotMatch || error == UidInvalid;
  if (m_session.recovering() && !rejected) {
    HttpMgr::GetInstance()->CancelRequest(ID_LOGIN_USER, LOGINMOD);
    TcpMgr::GetInstance()->CloseConnection();
    finishAuth();
    m_session.retryFailed();
    return;
  }
  const bool wasActive = sessionActive();
  if (rejected)
    m_account.signedOut();
  resetSession();
  if (wasActive)
    emit forceLogout(message);
  else
    emit loginHttpResult(error, message);
}
