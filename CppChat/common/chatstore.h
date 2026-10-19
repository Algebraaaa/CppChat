#ifndef CHATSTORE_H
#define CHATSTORE_H

#include "favorite.h"
#include "localorganizer.h"
#include "userdata.h"
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QVector>
#include <atomic>
#include <functional>
#include <limits>
#include <memory>

class QSqlDatabase;

// 工作线程只接收值快照，不读取 GUI 线程的 UserMgr 或共享消息对象。
struct StoredMessage {
  int threadId = 0;
  int senderUid = 0;
  int serverId = 0;
  QString uniqueId;
  QString content;
  QString time;
  int type = 0;
  int status = 0;
  QByteArray localImage;
  qint64 localId = 0;
  static StoredMessage From(const std::shared_ptr<ChatDataBase> &message);
  std::shared_ptr<TextChatData> ToMessage() const;
};

struct StoredConversation {
  int threadId = 0;
  int otherUid = 0;
  int lastServerId = 0;
  QString lastTime;
  QString summary;
  QString groupJson;
};

struct HistoryCursor {
  int serverId = std::numeric_limits<int>::max();
  qint64 localId = std::numeric_limits<qint64>::max();
  QString time;
  qint64 ceilingId = 0;
};

struct HistoryFilter {
  int threadId = 0;
  QString text;
  int category = -1;
  QString date;
  QVector<int> allowedThreads;
  bool confirmedOnly = false;
};

struct ChatStoreResult {
  struct Attachment {
    QString path;
    QString checksum;
    qint64 size = 0;
  } attachment;
  QString error;
  QVector<StoredMessage> messages;
  QVector<StoredConversation> conversations;
  QVector<StoredFavorite> favorites;
  QVector<LocalNote> notes;
  QVector<LocalReminder> reminders;
  QHash<int, int> cursors;
  QSet<QString> newMessageKeys;
  QSet<QString> suppressedMessageKeys;
  QVector<MsgInfo> draft;
  QByteArray image;
  bool more = false;
  HistoryCursor nextCursor;
};

class ChatStore : public QObject {
  Q_OBJECT
public:
  using Callback = std::function<void(const ChatStoreResult &)>;
  static ChatStore *instance();
  ~ChatStore() override;
  void OpenAccount(int uid, const QString &server, Callback callback);
  void CloseAccount();
  void Shutdown();
  bool IsReady() const { return _ready; }
  quint64 Generation() const { return _generation; }
  QString AccountDirectory() const { return _directory; }
  void SaveConversations(const QVector<StoredConversation> &conversations, Callback callback = {});
  void ForgetGroupProfile(int threadId, Callback callback = {});
  void SaveMessages(const QVector<StoredMessage> &messages, int syncThread = 0, int cursor = -1,
                    Callback callback = {}, int clearDraftThread = 0);
  void LoadMessages(int thread, int beforeId, int limit, Callback callback);
  void SearchMessages(int thread, const QString &query, int type, HistoryCursor cursor,
                      Callback callback, const QString &date = {},
                      const QVector<int> &allowedThreads = {});
  void QueryHistory(const HistoryFilter &filter, HistoryCursor cursor, Callback callback);
  void SaveDraft(int thread, const QVector<MsgInfo> &draft);
  void LoadDraft(int thread, Callback callback);
  void MarkUnconfirmed(const QString &id, bool failed);
  void LoadPendingImage(const QString &id, Callback callback);
  void SaveAttachment(const QString &id, const QByteArray &image, Callback callback = {});
  void LoadAttachment(const QString &id, Callback callback);
  void ClearLocalHistory(int thread, Callback callback, bool requireConfirmedOnly = false);
  void DeleteLocalMessage(int thread, int serverId, Callback callback = {});
  void DeleteLocalMessages(int thread, const QVector<int> &serverIds, Callback callback = {});
  void SaveFavorites(const QVector<StoredFavorite> &favorites, Callback callback = {});
  void LoadFavorites(const QString &query, FavoriteCategory category, qint64 beforeId,
                     Callback callback);
  void GetFavorite(qint64 id, Callback callback);
  void RemoveFavorites(const QVector<qint64> &ids, Callback callback = {});
  void SaveNote(const LocalNote &note, Callback callback);
  void LoadNotes(const QString &query, qint64 beforeId, Callback callback);
  void GetNote(qint64 id, Callback callback);
  void DeleteNote(qint64 id, int revision, Callback callback);
  void SaveReminder(const LocalReminder &reminder, Callback callback);
  void LoadReminders(Callback callback);
  void CheckDueReminders(qint64 now, Callback callback);
  void RemoveReminder(qint64 id, Callback callback);

signals:
  void storageError(const QString &message);
  void localMessageDeleted(int thread, int serverId);
  void localMessagesDeleted(int thread, QVector<int> serverIds);
  void localHistoryCleared(int thread);
  void favoritesChanged();
  void accountChanged();
  void accountReady();
  void notesChanged();
  void remindersChanged();

private:
  explicit ChatStore(QObject *parent);
  struct Worker;
  void Execute(std::function<ChatStoreResult(QSqlDatabase &)> operation, Callback callback);
  void ExecuteFile(std::function<ChatStoreResult()> operation, qint64 retainedBytes,
                   Callback callback);
  QThread _thread;
  QThreadPool _attachmentPool;
  std::shared_ptr<std::atomic_bool> _attachmentCancelled;
  int _attachmentTasks = 0;
  qint64 _attachmentBytes = 0;
  Worker *_worker;
  quint64 _generation = 0;
  bool _ready = false;
  QString _directory;
};

#endif
