#include "chatsyncservice.h"
#include "common/chatstore.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include <QJsonDocument>
#include <QPointer>
#include <algorithm>
#include <functional>
#include <utility>

ChatSyncService::ChatSyncService(QObject *parent) : QObject(parent)
{
  const auto tcp = TcpMgr::GetInstance();
  connect(tcp.get(), &TcpMgr::sig_load_chat_thread, this, &ChatSyncService::ReceiveThreads);
  connect(tcp.get(), &TcpMgr::sig_load_chat_msg, this, &ChatSyncService::ReceiveHistory);
  connect(tcp.get(), &TcpMgr::sig_request_failed, this, &ChatSyncService::RequestFailed);
  connect(tcp.get(), &TcpMgr::sig_text_chat_msg, this,
          [this](const auto &messages) { ReceiveMessages(messages, false); });
  connect(tcp.get(), &TcpMgr::sig_chat_msg_rsp, this,
          [this](int, const auto &messages) { ReceiveMessages(messages, true); });
  _syncTimer.setInterval(60000);
  connect(&_syncTimer, &QTimer::timeout, this, [this] { RefreshAllHistory(true); });
}

void ChatSyncService::StartSession(int uid, const QString &server)
{
  ResetSession();
  const auto revision = _revision;
  const QPointer<ChatSyncService> guard(this);
  ChatStore::instance()->OpenAccount(uid, server, [guard, revision](const ChatStoreResult &result) {
    if (!guard || guard->_revision != revision)
      return;
    if (!result.error.isEmpty()) {
      emit guard->initializationFailed(result.error);
      return;
    }
    guard->_storeReady = true;
    guard->_syncCursors = result.cursors;
    for (const auto &stored : result.conversations)
      if (auto data = guard->EnsureThread(stored.threadId, stored.otherUid))
        data->SetLastChatSummary(stored.lastServerId, stored.lastTime);
    emit guard->conversationsChanged(false);
    guard->LoadThreads();
    guard->LoadNextHistory();
    guard->_syncTimer.start();
  });
}

void ChatSyncService::ResetSession()
{
  ++_revision;
  ++_historyOperation;
  _syncTimer.stop();
  TcpMgr::GetInstance()->CancelRequest(ID_LOAD_CHAT_THREAD_REQ);
  TcpMgr::GetInstance()->CancelRequest(ID_LOAD_CHAT_MSG_REQ);
  _storeReady = _localRefreshScheduled = _threadsLoading = _threadsComplete = false;
  _localRefreshRequests.clear();
  _localLoads.clear();
  _syncCursors.clear();
  _historyQueue.clear();
  _historyComplete.clear();
  _threadsWithoutSummary.clear();
  _threadRevisions.clear();
  _threadCursor = _historyThread = _historyCursor = 0;
  ChatStore::instance()->CloseAccount();
}

void ChatSyncService::ReloadThreads()
{
  _threadsComplete = false;
  _threadCursor = 0;
  LoadThreads();
}

void ChatSyncService::RefreshAllHistory(bool force)
{
  if (!_storeReady || !TcpMgr::GetInstance()->IsConnected())
    return;
  for (const auto &data : UserMgr::GetInstance()->GetChatThreads()) {
    if (!data)
      continue;
    if (force)
      _historyComplete.remove(data->GetThreadId());
    QueueHistory(data->GetThreadId());
  }
}

void ChatSyncService::RemoveThread(int threadId)
{
  ++_threadRevisions[threadId];
  _localRefreshRequests.remove(threadId);
  _localLoads.remove(threadId);
  _syncCursors.remove(threadId);
  _historyQueue.removeAll(threadId);
  _historyComplete.remove(threadId);
  _threadsWithoutSummary.remove(threadId);
  if (_historyThread == threadId) {
    ++_historyOperation;
    TcpMgr::GetInstance()->CancelRequest(ID_LOAD_CHAT_MSG_REQ);
    _historyThread = _historyCursor = 0;
    QTimer::singleShot(0, this, &ChatSyncService::LoadNextHistory);
  }
}

void ChatSyncService::RequestFailed(ReqId request, const QString &message)
{
  if (!_storeReady)
    return;
  if (request == ID_LOAD_CHAT_THREAD_REQ)
    _threadsLoading = false;
  else if (request == ID_LOAD_CHAT_MSG_REQ) {
    ++_historyOperation;
    _historyThread = 0;
    QTimer::singleShot(0, this, &ChatSyncService::LoadNextHistory);
  } else
    return;
  emit status(message);
}

std::shared_ptr<ChatThreadData> ChatSyncService::EnsureThread(int threadId, int otherUid)
{
  if (threadId <= 0 || otherUid <= 0 || otherUid == UserMgr::GetInstance()->GetUid() ||
      !UserMgr::GetInstance()->CheckFriendById(otherUid))
    return nullptr;
  const auto mgr = UserMgr::GetInstance();
  auto data = mgr->GetChatThreadByThreadId(threadId);
  if (!data) {
    data = std::make_shared<ChatThreadData>(otherUid, threadId, 0);
    mgr->AddChatThreadData(data, otherUid);
    ChatStore::instance()->SaveConversations({{threadId, otherUid, 0, {}, {}}});
    if (_storeReady)
      LoadLocalMessages(threadId);
  }
  return data;
}
void ChatSyncService::LoadThreads()
{
  if (!_storeReady || _threadsLoading || _threadsComplete || !TcpMgr::GetInstance()->IsConnected())
    return;
  _threadsLoading = true;
  const QJsonObject request{{"uid", UserMgr::GetInstance()->GetUid()},
                            {"thread_id", _threadCursor}};
  emit TcpMgr::GetInstance() -> sig_send_data(
      ID_LOAD_CHAT_THREAD_REQ, QJsonDocument(request).toJson(QJsonDocument::Compact));
}
void ChatSyncService::ReceiveThreads(bool more, int lastId,
                                     std::vector<std::shared_ptr<ChatThreadInfo>> threads)
{
  if (!_threadsLoading)
    return;
  _threadsLoading = false;
  QVector<StoredConversation> storedThreads;
  for (const auto &thread : threads) {
    if (thread->_type != "private")
      continue; // 参考项目群聊仅有占位，尚无完整实现。
    const int self = UserMgr::GetInstance()->GetUid();
    if (thread->_user1_id != self && thread->_user2_id != self)
      continue;
    const int other = thread->_user1_id == self ? thread->_user2_id : thread->_user1_id;
    if (auto data = EnsureThread(thread->_thread_id, other)) {
      data->SetLastChatSummary(thread->_last_message_id, thread->_last_chat_time);
      storedThreads.append(
          {thread->_thread_id, other, thread->_last_message_id, thread->_last_chat_time, {}});
      if (!thread->_has_last_chat_summary ||
          (thread->_last_message_id > 0 && thread->_last_chat_time.isEmpty()))
        _threadsWithoutSummary.insert(thread->_thread_id);
    }
  }
  ChatStore::instance()->SaveConversations(storedThreads);
  if (more && lastId <= _threadCursor) {
    emit status(tr("会话分页游标未前进，请重新登录后重试"));
    return;
  }
  _threadCursor = lastId;
  _threadsComplete = !more;
  if (more)
    QTimer::singleShot(0, this, &ChatSyncService::LoadThreads);
  else {
    emit conversationsChanged(true);
    QList<int> missing = _threadsWithoutSummary.values();
    std::sort(missing.begin(), missing.end(), std::greater<int>());
    for (int threadId : missing)
      QueueHistory(threadId);
    _threadsWithoutSummary.clear();
    for (const auto &data : UserMgr::GetInstance()->GetChatThreads())
      if (data)
        QueueHistory(data->GetThreadId());
  }
}
void ChatSyncService::QueueHistory(int threadId, bool prioritize)
{
  if (_historyComplete.contains(threadId) || _historyThread == threadId)
    return;
  if (_historyQueue.contains(threadId)) {
    if (!prioritize)
      return;
    _historyQueue.removeAll(threadId);
  }
  if (prioritize)
    _historyQueue.prepend(threadId);
  else
    _historyQueue.enqueue(threadId);
  LoadNextHistory();
}
void ChatSyncService::LoadNextHistory()
{
  if (!_storeReady || _historyThread != 0 || _historyQueue.isEmpty() ||
      !TcpMgr::GetInstance()->IsConnected())
    return;
  ++_historyOperation;
  _historyThread = _historyQueue.dequeue();
  _historyCursor = _syncCursors.value(_historyThread);
  SendHistoryRequest();
}
void ChatSyncService::SendHistoryRequest()
{
  const QJsonObject request{{"thread_id", _historyThread}, {"message_id", _historyCursor}};
  emit TcpMgr::GetInstance()
      -> sig_send_data(ID_LOAD_CHAT_MSG_REQ, QJsonDocument(request).toJson(QJsonDocument::Compact));
}
void ChatSyncService::LoadLocalMessages(int threadId)
{
  if (!_storeReady)
    return;
  _localRefreshRequests.insert(threadId);
  if (_localRefreshScheduled)
    return;
  _localRefreshScheduled = true;
  const auto revision = _revision;
  QTimer::singleShot(0, this, [this, revision] {
    if (!IsCurrent(revision))
      return;
    _localRefreshScheduled = false;
    const auto requests = std::move(_localRefreshRequests);
    _localRefreshRequests.clear();
    for (int thread : requests) {
      if (_localLoads.contains(thread))
        _localRefreshRequests.insert(thread);
      else
        ReadLocalMessages(thread);
    }
  });
}

void ChatSyncService::ReadLocalMessages(int threadId)
{
  const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId);
  if (!data)
    return;
  _localLoads.insert(threadId);
  const QPointer<ChatSyncService> guard(this);
  const auto revision = _revision;
  const auto threadRevision = _threadRevisions.value(threadId);
  ChatStore::instance()->LoadMessages(
      threadId, 0, 50,
      [guard, revision, threadRevision, data, threadId](const ChatStoreResult &result) {
        if (!guard || !guard->IsCurrent(revision) ||
            guard->_threadRevisions.value(threadId) != threadRevision)
          return;
        guard->_localLoads.remove(threadId);
        // 查询期间又收到新消息时，只保留一个补读任务，持续收消息也正常刷新界面。
        if (guard->_localRefreshRequests.contains(threadId))
          guard->LoadLocalMessages(threadId);
        if (!result.error.isEmpty() ||
            UserMgr::GetInstance()->GetChatThreadByThreadId(threadId) != data)
          return;
        data->ClearLocalMessages();
        for (const auto &stored : result.messages) {
          const auto message = stored.ToMessage();
          if (stored.serverId > 0)
            data->MoveMsg(message);
          else if (!data->GetMsgUnRspRef().contains(stored.uniqueId))
            data->AppendUnRspMsg(stored.uniqueId, message);
        }
        emit guard->messagesChanged(threadId);
      });
}

void ChatSyncService::ReceiveHistory(int threadId, int messageId, bool more,
                                     std::vector<std::shared_ptr<TextChatData>> messages)
{
  if (threadId != _historyThread)
    return;
  if (!UserMgr::GetInstance()->GetChatThreadByThreadId(threadId)) {
    _historyThread = 0;
    LoadNextHistory();
    return;
  }
  int previous = _historyCursor;
  QVector<StoredMessage> records;
  for (const auto &message : messages) {
    if (message->GetThreadId() != threadId || message->GetMsgId() <= previous) {
      RequestFailed(ID_LOAD_CHAT_MSG_REQ, tr("历史消息顺序错误，未推进本地同步位置"));
      return;
    }
    previous = message->GetMsgId();
    records.append(StoredMessage::From(message));
  }
  if (messageId != previous || (more && messages.empty())) {
    RequestFailed(ID_LOAD_CHAT_MSG_REQ, tr("历史消息游标错误，未推进本地同步位置"));
    return;
  }
  const QPointer<ChatSyncService> guard(this);
  const auto revision = _revision;
  // 一页消息和同步位置必须在同一个事务中提交，提交失败时保留原游标。
  const auto operation = _historyOperation;
  ChatStore::instance()->SaveMessages(
      records, threadId, messageId,
      [guard, revision, operation, threadId, messageId, more,
       messages](const ChatStoreResult &result) {
        if (!guard || !guard->IsCurrent(revision) || guard->_historyOperation != operation ||
            guard->_historyThread != threadId)
          return;
        if (!result.error.isEmpty()) {
          guard->RequestFailed(ID_LOAD_CHAT_MSG_REQ,
                               guard->tr("历史记录未保存，请检查本地磁盘后重试"));
          return;
        }
        guard->_syncCursors.insert(threadId, messageId);
        guard->_historyCursor = messageId;
        if (const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId))
          for (const auto &message : messages)
            data->MoveMsg(message);
        guard->LoadLocalMessages(threadId);
        if (more) {
          guard->SendHistoryRequest();
          return;
        }
        guard->_historyComplete.insert(threadId);
        guard->_historyThread = 0;
        guard->LoadNextHistory();
      });
}

void ChatSyncService::ReceiveMessages(const std::vector<std::shared_ptr<TextChatData>> &messages,
                                      bool reply)
{
  if (!_storeReady)
    return;
  QVector<StoredMessage> records;
  std::vector<std::shared_ptr<TextChatData>> accepted;
  QHash<int, std::shared_ptr<ChatThreadData>> threads;
  QHash<int, quint64> threadRevisions;
  for (const auto &message : messages) {
    auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(message->GetThreadId());
    if (!data && !reply)
      data = EnsureThread(message->GetThreadId(), message->GetSendUid());
    if (!data)
      continue;
    records.append(StoredMessage::From(message));
    accepted.push_back(message);
    threads.insert(message->GetThreadId(), data);
    threadRevisions.insert(message->GetThreadId(), _threadRevisions.value(message->GetThreadId()));
  }
  if (records.isEmpty())
    return;
  const QPointer<ChatSyncService> guard(this);
  const auto revision = _revision;
  // 实时最大 ID 不代表中间消息都已拉齐，因此此处不更新同步游标。
  ChatStore::instance()->SaveMessages(
      records, 0, -1,
      [guard, revision, accepted, threads, threadRevisions, reply](const ChatStoreResult &result) {
        if (!guard || !guard->IsCurrent(revision) || !result.error.isEmpty())
          return;
        QSet<int> refresh;
        for (const auto &message : accepted) {
          const int thread = message->GetThreadId();
          const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(thread);
          if (!data || data != threads.value(thread) ||
              guard->_threadRevisions.value(thread) != threadRevisions.value(thread))
            continue;
          const bool known = !result.newMessageKeys.contains(QString::number(thread) + ':' +
                                                             QString::number(message->GetMsgId()));
          data->MoveMsg(message);
          if (!reply && !known)
            emit guard->incomingMessage(thread);
          refresh.insert(thread);
        }
        for (int thread : refresh)
          guard->LoadLocalMessages(thread);
      });
}
