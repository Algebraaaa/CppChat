#include "appbridge.h"
#include "mobilemedia.h"
#include "common/attachmentpreparer.h"
#include "common/chatstore.h"
#include "common/conversationpreferences.h"
#include "network/resourcemgr.h"
#include "network/httpmgr.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/chatsyncservice.h"
#include "services/messagesender.h"

#include <QBuffer>
#include <QJsonDocument>
#include <QTimer>
#include <algorithm>

namespace {
QByteArray packet(const QJsonObject &object)
{
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
QString imageDataUrl(const QByteArray &bytes)
{
  return bytes.isEmpty() ? QString()
                         : "data:image/png;base64," + QString::fromLatin1(bytes.toBase64());
}
QVariantMap userMap(int uid, const QString &name, const QString &nick, const QString &icon)
{
  return {{"uid", uid},
          {"name", name},
          {"nick", nick},
          {"icon", icon},
          {"displayName", nick.isEmpty() ? name : nick}};
}
} // namespace

void AppBridge::connectChatBackends()
{
  const auto tcp = TcpMgr::GetInstance();
  connect(tcp.get(), &TcpMgr::sig_con_success, this, [this](bool success) {
    if (m_authRequest != ID_LOGIN_USER)
      return;
    if (success) {
      TcpMgr::GetInstance()->slot_send_data(ID_CHAT_LOGIN, packet({{"uid", m_loginServer.Uid},
                                                                   {"token", m_loginServer.Token},
                                                                   {"device", "android"}}));
    } else {
      loginFailed(NetworkError, tr("连接聊天服务失败，请稍后重试"));
    }
  });
  connect(tcp.get(), &TcpMgr::sig_login_failed, this, [this](int error) {
    loginFailed(error, errorCodeMessage(error));
  });
  connect(tcp.get(), &TcpMgr::sig_swich_chatdlg, this, [this] {
    m_sync->StartSession(currentUid(), m_gateUrl);
    emit loginStatusChanged();
  });
  connect(m_sync, &ChatSyncService::conversationsChanged, this, [this](bool) {
    emit chatThreadsLoaded();
    emit loginStatusChanged();
    if (authBusy() && storeReady()) {
      const bool recoveringSession = m_session.active();
      authenticatedAccount();
      m_session.authenticated();
      finishAuth();
      emit contactsChanged();
      if (!recoveringSession)
        emit chatSwitched();
    }
  });
  connect(m_sync, &ChatSyncService::initializationFailed, this, [this](const QString &message) {
    const bool wasActive = sessionActive();
    resetSession();
    const auto error = tr("本地聊天记录初始化失败：%1").arg(message);
    if (wasActive)
      emit forceLogout(error);
    else
      emit loginHttpResult(NetworkError, error);
  });
  connect(m_sync, &ChatSyncService::messagesChanged, this, [this](int thread) {
    emit messagesLoaded(thread);
    emit chatThreadsLoaded();
  });
  connect(m_sync, &ChatSyncService::status, this, [this](const QString &message) {
    if (!message.isEmpty())
      emit statusMessage(message);
  });
  connect(m_sender, &MessageSender::messagesChanged, this, [this](int thread, bool) {
    emit messagesLoaded(thread);
    emit chatThreadsLoaded();
  });
  connect(m_sender, &MessageSender::messageStatusChanged, this,
          [this](int thread, const QString &, int) { emit messagesLoaded(thread); });
  connect(m_sender, &MessageSender::savingChanged, this, &AppBridge::sendingChanged);
  connect(m_sender, &MessageSender::outgoingSaved, this, [this](int thread) {
    if (thread == m_pendingTextThread) {
      const auto text = m_pendingText;
      m_pendingText.clear();
      m_pendingTextThread = 0;
      emit textMessageSaved(thread, text);
    }
  });
  connect(AttachmentPreparer::instance(), &AttachmentPreparer::prepared, this,
          [this](const QString &id) {
            if (!m_pendingImage || m_pendingImage->id != id)
              return;
            const auto preparation = m_pendingImage;
            const auto thread = m_imageThread;
            m_pendingImage.reset();
            m_imageThread.reset();
            if (!preparation->error.isEmpty()) {
              emit operationFailed(preparation->error);
            } else if (thread && UserMgr::GetInstance()->GetChatThreadByThreadId(
                                     thread->GetThreadId()) == thread) {
              MsgInfo image;
              image.msgFlag = m_attachmentKind;
              image.imageData = preparation->bytes;
              m_sender->Send({image}, thread, false);
            }
            emit sendingChanged();
          });
  connect(m_sender, &MessageSender::status, this, [this](int, const QString &message) {
    if (!message.isEmpty())
      emit statusMessage(message);
  });
  connect(tcp.get(), &TcpMgr::sig_notify_offline, this, [this] {
    resetSession();
    m_account.signedOut();
    emit forceLogout(tr("登录状态已失效，请重新登录"));
  });
  connect(tcp.get(), &TcpMgr::sig_connection_closed, this, [this] {
    connectionInterrupted();
  });
  connect(tcp.get(), &TcpMgr::sig_request_failed, this,
          [this](ReqId request, const QString &message) {
            if (request == ID_CREATE_PRIVATE_CHAT_REQ)
              m_creatingUid = 0;
            emit operationFailed(message);
          });
  connect(tcp.get(), &TcpMgr::sig_create_private_chat, this,
          [this](int uid, int other, int thread) {
            if (uid != currentUid() || other != m_creatingUid)
              return;
            m_creatingUid = 0;
            if (m_sync->EnsureThread(thread, other)) {
              emit chatThreadsLoaded();
              emit chatOpened(thread, threadInfo(thread).value("name").toString());
            }
          });
  connect(tcp.get(), &TcpMgr::sig_user_search, this, [this](std::shared_ptr<SearchInfo> info) {
    m_searchResult = info;
    auto result = info ? userMap(info->_uid, info->_name, info->_nick, info->_icon) : QVariantMap();
    if (info) {
      result["description"] = info->_desc;
      result["canAdd"] =
          info->_uid != currentUid() && !UserMgr::GetInstance()->CheckFriendById(info->_uid);
    }
    emit friendSearchResult(result);
  });
  connect(tcp.get(), &TcpMgr::sig_add_friend_rsp, this, &AppBridge::friendApplySent);
  connect(tcp.get(), &TcpMgr::sig_friend_apply, this,
          [this](const auto &) { emit contactsChanged(); });
  connect(tcp.get(), &TcpMgr::sig_auth_rsp, this, [this](const std::shared_ptr<AuthRsp> &info) {
    m_sync->EnsureThread(info->_thread_id, info->_uid);
    m_sync->ReceiveMessages(info->_chat_datas, false);
    emit contactsChanged();
    emit chatThreadsLoaded();
  });
  connect(tcp.get(), &TcpMgr::sig_add_auth_friend, this,
          [this](const std::shared_ptr<AuthInfo> &info) {
            m_sync->EnsureThread(info->_thread_id, info->_uid);
            m_sync->ReceiveMessages(info->_chat_datas, false);
            emit contactsChanged();
            emit chatThreadsLoaded();
          });
  connect(tcp.get(), &TcpMgr::sig_friend_deleted, this, [this](int uid) {
    const int thread = UserMgr::GetInstance()->RemoveFriend(uid);
    m_sender->RemoveThread(thread);
    m_sync->RemoveThread(thread);
    emit contactsChanged();
    emit chatThreadsLoaded();
    emit messagesLoaded(thread);
  });
  connect(UserMgr::GetInstance().get(), &UserMgr::sig_avatar_ready, this, &AppBridge::avatarReady);
  connect(ResourceMgr::instance(), &ResourceMgr::imageReady, this,
          [this](const QString &id, const QByteArray &, const QString &error) {
            if (error.isEmpty())
              emit imageReady(id);
            else
              emit operationFailed(error);
          });
}

void AppBridge::resetSession()
{
  if (m_media)
    m_media->reset();
  m_requestedFiles.clear();
  m_savingVoice.clear();
  m_session.stop();
  HttpMgr::GetInstance()->CancelRequest(ID_LOGIN_USER, LOGINMOD);
  m_account.endSession();
  m_pendingImage.reset();
  m_imageThread.reset();
  m_pendingText.clear();
  m_pendingTextThread = 0;
  if (m_sender)
    m_sender->Reset();
  if (m_sync)
    m_sync->ResetSession();
  ResourceMgr::instance()->ResetSession();
  TcpMgr::GetInstance()->CloseConnection();
  UserMgr::GetInstance()->Reset();
  m_creatingUid = 0;
  m_searchResult.reset();
  finishAuth();
  emit loginStatusChanged();
}

void AppBridge::disconnectChat()
{
  m_account.signedOut();
  resetSession();
  emit forceLogout(QString());
}

void AppBridge::loadChatThreads()
{
  if (storeReady()) {
    m_sync->ReloadThreads();
    m_sync->RefreshAllHistory(true);
  }
}

void AppBridge::loadMessages(int threadId, int beforeId, int limit)
{
  if (!storeReady())
    return;
  const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId);
  if (!data)
    return;
  // The shared service owns forward server synchronization. This separate read
  // only expands the visible local window, and never advances a sync cursor.
  ChatStore::instance()->LoadMessages(
      threadId, beforeId, qBound(1, limit, 200),
      [this, threadId, data](const ChatStoreResult &result) {
        if (!result.error.isEmpty() ||
            UserMgr::GetInstance()->GetChatThreadByThreadId(threadId) != data)
          return;
        for (const auto &stored : result.messages) {
          auto message = stored.ToMessage();
          if (stored.serverId > 0)
            data->MoveMsg(message);
          else if (!data->GetMsgUnRspRef().contains(stored.uniqueId))
            data->AppendUnRspMsg(stored.uniqueId, message);
        }
        emit messagesLoaded(threadId);
        emit localHistoryLoaded(threadId, result.more);
      });
  m_sync->QueueHistory(threadId, true);
}

bool AppBridge::sendTextMessage(int threadId, const QString &text)
{
  const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId);
  if (sending() || !storeReady() || !isLoggedIn() || !data || text.trimmed().isEmpty())
    return false;
  MsgInfo message;
  message.msgFlag = "text";
  message.content = text;
  m_pendingText = text;
  m_pendingTextThread = threadId;
  if (m_sender->Send({message}, data))
    return true;
  m_pendingText.clear();
  m_pendingTextThread = 0;
  return false;
}

bool AppBridge::sendImage(int threadId, const QUrl &file)
{
  const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId);
  if (sending() || !storeReady() || !isLoggedIn() || !data || !file.isValid())
    return false;
  m_imageThread = data;
  m_attachmentKind = "image";
  m_pendingImage = AttachmentPreparer::instance()->FromFile(file.isLocalFile() ? file.toLocalFile()
                                                                               : file.toString());
  if (m_pendingImage->ready) {
    const auto error = m_pendingImage->error;
    m_pendingImage.reset();
    m_imageThread.reset();
    emit operationFailed(error);
    return false;
  }
  emit sendingChanged();
  return true;
}

void AppBridge::retryMessage(int threadId, const QString &id)
{
  const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId);
  if (data && storeReady()) {
    const auto message = std::dynamic_pointer_cast<TextChatData>(data->GetMsgUnRspRef().value(id));
    if (message)
      m_sender->RetryMessage(message, data);
  }
}

void AppBridge::openChat(int uid)
{
  if (!isLoggedIn() || !storeReady() || !UserMgr::GetInstance()->CheckFriendById(uid))
    return;
  const auto data = UserMgr::GetInstance()->GetChatThreadByUid(uid);
  if (data) {
    emit chatOpened(data->GetThreadId(), threadInfo(data->GetThreadId()).value("name").toString());
    return;
  }
  if (m_creatingUid)
    return;
  m_creatingUid = uid;
  TcpMgr::GetInstance()->slot_send_data(ID_CREATE_PRIVATE_CHAT_REQ,
                                        packet({{"uid", currentUid()}, {"other_id", uid}}));
}

void AppBridge::searchUser(const QString &keyword)
{
  if (isLoggedIn() && !keyword.trimmed().isEmpty())
    TcpMgr::GetInstance()->slot_send_data(ID_SEARCH_USER_REQ, packet({{"uid", keyword.trimmed()}}));
}

void AppBridge::addFriendApply(int uid, const QString &description)
{
  if (!isLoggedIn() || !m_searchResult || m_searchResult->_uid != uid || uid == currentUid() ||
      UserMgr::GetInstance()->CheckFriendById(uid))
    return;
  UserMgr::GetInstance()->RememberOutgoingApply(m_searchResult);
  TcpMgr::GetInstance()->slot_send_data(
      ID_ADD_FRIEND_REQ,
      packet(
          {{"uid", currentUid()},
           {"touid", uid},
           {"bakname", ""},
           {"applyname", description.trimmed().isEmpty() ? currentNick() : description.left(100)},
           {"tags", QJsonArray()}}));
}

void AppBridge::acceptFriendApply(int uid)
{
  if (isLoggedIn() && uid > 0 && uid != currentUid())
    TcpMgr::GetInstance()->slot_send_data(
        ID_AUTH_FRIEND_REQ,
        packet({{"fromuid", currentUid()}, {"touid", uid}, {"back", ""}, {"tags", QJsonArray()}}));
}

QVariantList AppBridge::friendList() const
{
  QVariantList result;
  for (const auto &user : UserMgr::GetInstance()->GetFriendList()) {
    auto item = userMap(user->_uid, user->_name, user->_nick, user->_icon);
    if (!user->_back.isEmpty())
      item["displayName"] = user->_back;
    result.append(item);
  }
  return result;
}

QVariantList AppBridge::applyList() const
{
  QVariantList result;
  for (const auto &user : UserMgr::GetInstance()->GetApplyList()) {
    auto item = userMap(user->_uid, user->_name, user->_nick, user->_icon);
    item["description"] = user->_desc;
    item["status"] = user->_status;
    result.append(item);
  }
  return result;
}

QVariantMap AppBridge::threadInfo(int threadId) const
{
  const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId);
  if (!data)
    return {};
  const auto user = UserMgr::GetInstance()->GetFriendById(data->GetOtherId());
  QString name = data->IsGroup() ? data->GetGroupName() : tr("用户 %1").arg(data->GetOtherId());
  if (user)
    name =
        !user->_back.isEmpty() ? user->_back : (user->_nick.isEmpty() ? user->_name : user->_nick);
  return {{"threadId", threadId},
          {"otherId", data->GetOtherId()},
          {"name", name},
          {"lastMsg", data->GetLastMsg()},
          {"lastTime", data->GetLastChatTime()},
          {"icon", user ? user->_icon : QString()},
          {"isGroup", data->IsGroup()},
          {"pinned", ConversationPreferences::Load(currentUid()).pinned.contains(threadId)}};
}

QVariantList AppBridge::chatThreads() const
{
  QVariantList result;
  for (const auto &data : UserMgr::GetInstance()->GetChatThreads())
    result.append(threadInfo(data->GetThreadId()));
  std::sort(result.begin(), result.end(), [](const QVariant &left, const QVariant &right) {
    if (left.toMap().value("pinned").toBool() != right.toMap().value("pinned").toBool())
      return left.toMap().value("pinned").toBool();
    return left.toMap().value("lastTime").toString() > right.toMap().value("lastTime").toString();
  });
  return result;
}

QVariantList AppBridge::messagesForThread(int threadId) const
{
  QVariantList result;
  const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId);
  if (!data)
    return result;
  const auto append = [this, &result](const std::shared_ptr<ChatDataBase> &message) {
    result.append(QVariantMap{{"msgId", message->GetMsgId()},
                              {"uniqueId", message->GetUniqueId()},
                              {"threadId", message->GetThreadId()},
                              {"senderUid", message->GetSendUid()},
                              {"content", message->GetContent()},
                              {"time", message->GetChatTime()},
                              {"isSelf", message->GetSendUid() == currentUid()},
                              {"type", static_cast<int>(message->GetMsgType())},
                              {"status", message->GetStatus()}});
  };
  for (const auto &message : data->GetMsgMap())
    append(message);
  for (const auto &message : data->GetPendingMessages())
    append(message);
  return result;
}

QString AppBridge::avatarSource(int uid, const QString &fallback)
{
  const auto image = UserMgr::GetInstance()->AvatarThumbnail(uid, fallback, QSize(96, 96));
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return imageDataUrl(bytes);
}

QString AppBridge::imageSource(const QString &id)
{
  auto resource = ResourceMgr::instance();
  const auto bytes = resource->cachedImage(id);
  if (bytes.isEmpty() && !id.isEmpty())
    resource->fetchImage(id);
  return imageDataUrl(bytes);
}
