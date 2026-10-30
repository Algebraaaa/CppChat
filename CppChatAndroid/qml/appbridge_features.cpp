#include "appbridge.h"
#include "common/chatstore.h"
#include "common/conversationpreferences.h"
#include "common/runtimepaths.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/chatsyncservice.h"
#include "services/clientupdateservice.h"
#include "../update/mobileupdateservice.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <QUuid>

void AppBridge::initializeFeatures()
{
  connect(&m_account, &MobileAccount::failed, this, &AppBridge::operationFailed);
  m_account.initialize(m_gateUrl);
  connect(&m_account, &MobileAccount::changed, this, [this] {
    if (m_autoStarted && m_account.ready() && m_account.automatic() && !authBusy() &&
        !isLoggedIn()) {
      m_autoStarted = false;
      restoreAccount();
    }
  });
  m_updates = new MobileUpdateService(
      ClientUpdateService::manifestUrlForServer(QUrl(m_gateUrl), true), CPPCHAT_VERSION, this);
  auto lastCheck = std::make_shared<QElapsedTimer>();
  connect(m_updates, &MobileUpdateService::stateChanged, this, [this, lastCheck] {
    if (m_updates->state() == ClientUpdateService::State::Checking)
      lastCheck->start();
    emit updateChanged();
  });
  connect(m_updates, &MobileUpdateService::statusChanged, this, &AppBridge::updateChanged);
  connect(m_updates, &MobileUpdateService::failed, this, [this](const QString &message) {
    m_updateMessage = message;
    emit updateChanged();
    if (m_manualUpdate)
      emit operationFailed(message);
    m_manualUpdate = false;
  });
  connect(m_updates, &MobileUpdateService::checkFinished, this, [this](bool available) {
    m_updateMessage = available ? tr("发现新版本") : tr("当前已是最新版本");
    emit updateChanged();
    if (available || m_manualUpdate)
      emit statusMessage(m_updateMessage);
    m_manualUpdate = false;
  });
  connect(m_updates, &MobileUpdateService::downloadProgress, this,
          [this](qint64, qint64) { emit updateChanged(); });
  connect(m_updates, &MobileUpdateService::downloadReady, this, [this] {
    m_updateMessage = tr("安装包校验完成，可以安装");
    emit updateChanged();
    emit statusMessage(m_updateMessage);
  });
  auto timer = new QTimer(this);
  timer->setInterval(6 * 60 * 60 * 1000);
  const auto checkWhenDue = [this, lastCheck] {
    if (QGuiApplication::applicationState() == Qt::ApplicationActive &&
        (!lastCheck->isValid() || lastCheck->elapsed() >= 6 * 60 * 60 * 1000) &&
        m_updates->state() == ClientUpdateService::State::Idle)
      m_updates->check();
  };
  connect(timer, &QTimer::timeout, this, checkWhenDue);
  if (auto app = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
    connect(app, &QGuiApplication::applicationStateChanged, this,
            [this, checkWhenDue](Qt::ApplicationState state) {
              if (state == Qt::ApplicationActive) {
                m_updates->refresh();
                checkWhenDue();
              }
            });
  timer->start();
  const auto tcp = TcpMgr::GetInstance();
  connect(tcp.get(), &TcpMgr::sig_profile_updated, this, [this](const QJsonObject &profile) {
    const auto user = UserMgr::GetInstance()->GetUserInfo();
    if (!user || profile["uid"].toInt() != user->_uid)
      return;
    user->_name = profile["name"].toString();
    user->_nick = profile["nick"].toString();
    user->_desc = profile["desc"].toString();
    emit loginStatusChanged();
    emit statusMessage(tr("资料已保存"));
  });
  connect(tcp.get(), &TcpMgr::sig_group_result, this, [this](const QJsonObject &response) {
    emit groupResult(response.toVariantMap());
    if (response["error"].toInt() != 0)
      emit operationFailed(errorCodeMessage(response["error"].toInt()));
    else {
      m_sync->ReloadThreads();
      emit chatThreadsLoaded();
    }
  });
}
void AppBridge::startAutomaticLogin()
{
  m_autoStarted = true;
  if (m_account.ready()) {
    m_autoStarted = false;
    if (m_account.automatic() && !m_account.password().isEmpty())
      restoreAccount();
  }
  // The root page has connected its notifications by this point.
  QTimer::singleShot(3000, m_updates, &MobileUpdateService::check);
}
void AppBridge::authenticatedAccount()
{
  m_account.authenticated(m_loginEmail, m_loginPassword);
}
void AppBridge::restoreAccount()
{
  if (m_account.password().isEmpty() || sessionActive() || authBusy() || reconnecting())
    return;
  m_session.prepare(m_account.email(), m_account.password());
  m_session.interrupted();
  m_session.retryNow();
}
void AppBridge::checkUpdates()
{
  m_manualUpdate = true;
  m_updateMessage.clear();
  m_updates->check();
}
void AppBridge::downloadUpdate()
{
  m_updateMessage.clear();
  m_updates->download();
}
void AppBridge::cancelUpdate()
{
  if (m_updates->state() != ClientUpdateService::State::Downloading || m_updates->verifying())
    return;
  m_updates->cancel();
  m_updateMessage = m_updates->backgroundManaged() ? tr("已取消后台下载")
                                                   : tr("下载已暂停，本次运行中可继续下载");
  emit updateChanged();
}
void AppBridge::installUpdate()
{
  QString error;
  if (!m_updates->launchInstaller({}, error))
    emit operationFailed(error);
}
QVariantMap AppBridge::updateInfo() const
{
  return {{"current", CPPCHAT_VERSION},
          {"state", m_updates ? static_cast<int>(m_updates->state()) : 0},
          {"version", m_updates ? m_updates->info().version : QString()},
          {"notes", m_updates ? m_updates->releaseNotes() : QString()},
          {"message", m_updates && !m_updates->statusText().isEmpty() ? m_updates->statusText()
                                                                      : m_updateMessage},
          {"background", m_updates && m_updates->backgroundManaged()},
          {"verifying", m_updates && m_updates->verifying()},
          {"received", m_updates ? m_updates->downloadedBytes() : 0},
          {"total", m_updates ? m_updates->info().size : 0},
          {"bytesPerSecond", m_updates ? m_updates->bytesPerSecond() : 0}};
}
QVariantMap AppBridge::storageInfo() const
{
  const auto directory = ChatStore::instance()->AccountDirectory();
  qint64 bytes = 0;
  if (!directory.isEmpty()) {
    QDirIterator files(directory, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (files.hasNext()) {
      files.next();
      bytes += files.fileInfo().size();
    }
  }
  return {
      {"directory", directory},
      {"bytes", bytes},
      {"database", directory.isEmpty() ? QString() : directory + "/sql/db.sqlite"},
      {"description",
       tr("记录按账号保存在应用私有目录，其他应用不能直接读取。退出登录保留记录，卸载或清除应用数据会删除记录。电脑与手机分别保存，登录后从服务器补拉历史。")}};
}
void AppBridge::searchHistory(int threadId, const QString &query, int type, int beforeId,
                              qint64 beforeLocal)
{
  if (!storeReady())
    return;
  QPointer<AppBridge> guard(this);
  ChatStore::instance()->SearchMessages(
      threadId, query, type, {beforeId, beforeLocal},
      [guard, threadId](const ChatStoreResult &result) {
        if (!guard)
          return;
        if (!result.error.isEmpty()) {
          emit guard->operationFailed(tr("搜索本地记录失败"));
          return;
        }
        QVariantList messages;
        for (const auto &message : result.messages)
          messages.append(QVariantMap{{"content", message.content},
                                      {"time", message.time},
                                      {"type", message.type},
                                      {"senderUid", message.senderUid}});
        emit guard->historyResults(threadId, messages, result.more, result.nextCursor.serverId,
                                   result.nextCursor.localId);
      });
}
void AppBridge::clearLocalHistory(int threadId)
{
  if (!storeReady())
    return;
  auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId);
  if (!data)
    return;
  QPointer<AppBridge> guard(this);
  ChatStore::instance()->ClearLocalHistory(
      threadId, [guard, threadId, data](const ChatStoreResult &result) {
        if (!guard || UserMgr::GetInstance()->GetChatThreadByThreadId(threadId) != data)
          return;
        if (!result.error.isEmpty()) {
          emit guard->operationFailed(tr("清空本地记录失败"));
          return;
        }
        data->ClearLocalMessages();
        emit guard->historyCleared(threadId);
        emit guard->messagesLoaded(threadId);
        emit guard->chatThreadsLoaded();
      });
}
void AppBridge::saveDraft(int threadId, const QString &text)
{
  if (!storeReady())
    return;
  QVector<MsgInfo> draft;
  if (!text.isEmpty()) {
    MsgInfo input;
    input.msgFlag = "text";
    input.content = text;
    draft.append(input);
  }
  ChatStore::instance()->SaveDraft(threadId, draft);
}
void AppBridge::loadDraft(int threadId)
{
  if (!storeReady())
    return;
  QPointer<AppBridge> guard(this);
  ChatStore::instance()->LoadDraft(threadId, [guard, threadId](const ChatStoreResult &result) {
    if (!guard)
      return;
    QString text;
    for (const auto &input : result.draft)
      if (input.msgFlag == "text")
        text += input.content;
    emit guard->draftLoaded(threadId, text);
  });
}
void AppBridge::setPinned(int threadId, bool pinned)
{
  auto state = ConversationPreferences::Load(currentUid());
  if (pinned)
    state.pinned.insert(threadId);
  else
    state.pinned.remove(threadId);
  ConversationPreferences::SavePinned(currentUid(), state.pinned);
  emit chatThreadsLoaded();
}
void AppBridge::updateProfile(const QString &nick, const QString &description)
{
  if (!isLoggedIn())
    return;
  if (nick.trimmed().isEmpty() || nick.toUtf8().size() > 64 || description.toUtf8().size() > 255) {
    emit operationFailed(tr("昵称最多 64 字节，签名最多 255 字节"));
    return;
  }
  TcpMgr::GetInstance()->slot_send_data(
      ID_UPDATE_PROFILE_REQ,
      QJsonDocument(
          QJsonObject{{"name", currentUser()}, {"nick", nick.trimmed()}, {"desc", description}})
          .toJson(QJsonDocument::Compact));
}
void AppBridge::deleteFriend(int uid)
{
  if (isLoggedIn() && UserMgr::GetInstance()->CheckFriendById(uid))
    TcpMgr::GetInstance()->slot_send_data(
        ID_DELETE_FRIEND_REQ,
        QJsonDocument(QJsonObject{{"friend_uid", uid}}).toJson(QJsonDocument::Compact));
}
QVariantMap AppBridge::groupInfo(int threadId) const
{
  const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId);
  return data && data->IsGroup() ? data->Group().toVariantMap() : QVariantMap();
}
QString AppBridge::newGroupCreationKey() const
{
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
void AppBridge::groupCommand(const QString &operation, int threadId, const QVariantMap &fields)
{
  if (!storeReady() || !TcpMgr::GetInstance()->SupportsGroupChat()) {
    emit operationFailed(tr("当前服务尚未支持群聊"));
    return;
  }
  QJsonObject body = QJsonObject::fromVariantMap(fields);
  body["op"] = operation;
  if (threadId > 0)
    body["thread_id"] = threadId;
  if (operation == "create" && !body.contains("creation_key"))
    body["creation_key"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
  TcpMgr::GetInstance()->slot_send_data(ID_GROUP_REQ,
                                        QJsonDocument(body).toJson(QJsonDocument::Compact));
}
