#pragma once

#include "common/userdata.h"
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QTimer>

// Owns account-local restoration, server pagination and durable message merge.
// Navigation, unread indicators and widgets remain in ChatDialog.
class ChatSyncService : public QObject {
  Q_OBJECT
public:
  explicit ChatSyncService(QObject *parent = nullptr);
  void StartSession(int uid, const QString &server);
  void ResetSession();
  bool ThreadsComplete() const { return _threadsComplete; }
  bool IsReady() const { return _storeReady; }
  void LoadThreads();
  void ReloadThreads();
  std::shared_ptr<ChatThreadData> EnsureThread(int threadId, int otherUid);
  void QueueHistory(int threadId, bool prioritize = false);
  void RefreshAllHistory(bool force = false);
  void LoadLocalMessages(int threadId);
  void RemoveThread(int threadId);
  void ReceiveMessages(const std::vector<std::shared_ptr<TextChatData>> &messages, bool reply);

signals:
  void groupRemoved(int threadId);
  void conversationsChanged(bool complete);
  void messagesChanged(int threadId);
  void incomingMessage(int threadId);
  void initializationFailed(const QString &message);
  void status(const QString &message);

private:
  void ReceiveThreads(bool more, int lastId, std::vector<std::shared_ptr<ChatThreadInfo>> threads);
  void ReceiveHistory(int threadId, int messageId, bool more,
                      std::vector<std::shared_ptr<TextChatData>> messages);
  void LoadNextHistory();
  void SendHistoryRequest();
  void ReadLocalMessages(int threadId);
  void RequestFailed(ReqId request, const QString &message);
  bool IsCurrent(quint64 revision) const { return _revision == revision && _storeReady; }
  quint64 _revision = 0;
  quint64 _historyOperation = 0;
  QHash<int, quint64> _threadRevisions;
  QSet<int> _localRefreshRequests;
  QSet<int> _localLoads;
  bool _localRefreshScheduled = false;
  QHash<int, int> _syncCursors;
  QQueue<int> _historyQueue;
  QSet<int> _historyComplete;
  QSet<int> _threadsWithoutSummary;
  int _threadCursor = 0;
  int _historyThread = 0;
  int _historyCursor = 0;
  bool _storeReady = false;
  bool _threadsLoading = false;
  bool _threadsComplete = false;
  void ReceiveGroup(const QJsonObject &group);
  QSet<int> _groupSnapshot;
  QTimer _syncTimer;
  QTimer _groupTimer;
};
