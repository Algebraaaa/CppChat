#ifndef APPBRIDGE_H
#define APPBRIDGE_H

#include "common/userdata.h"
#include "mobileaccount.h"
#include "mobilesession.h"
#include "mobilemedia.h"
#include <QSet>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class ChatSyncService;
class MessageSender;
class ClientUpdateService;
class MobileMedia;

// Mobile presentation facade. The working desktop application's services own
// synchronization, durable sends, retries and the wire protocol.
class AppBridge : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool isLoggedIn READ isLoggedIn NOTIFY loginStatusChanged)
  Q_PROPERTY(bool sessionActive READ sessionActive NOTIFY loginStatusChanged)
  Q_PROPERTY(bool reconnecting READ reconnecting NOTIFY loginStatusChanged)
  Q_PROPERTY(QString connectionText READ connectionText NOTIFY loginStatusChanged)
  Q_PROPERTY(bool authBusy READ authBusy NOTIFY authBusyChanged)
  Q_PROPERTY(bool storeReady READ storeReady NOTIFY loginStatusChanged)
  Q_PROPERTY(bool sending READ sending NOTIFY sendingChanged)
  Q_PROPERTY(QString currentUser READ currentUser NOTIFY loginStatusChanged)
  Q_PROPERTY(QString currentNick READ currentNick NOTIFY loginStatusChanged)
  Q_PROPERTY(QString currentDescription READ currentDescription NOTIFY loginStatusChanged)
  Q_PROPERTY(MobileAccount *account READ account CONSTANT)
  Q_PROPERTY(QVariantMap updateInfo READ updateInfo NOTIFY updateChanged)
  Q_PROPERTY(MobileMedia *media READ media CONSTANT)
  Q_PROPERTY(QVariantMap mediaCapabilities READ mediaCapabilities NOTIFY loginStatusChanged)

public:
  explicit AppBridge(QObject *parent = nullptr);
  ~AppBridge() override;
  bool init(const QString &dataDirectory = QString());
  Q_INVOKABLE void requestVerifyCode(const QString &email);
  Q_INVOKABLE void registerUser(const QString &email, const QString &password,
                                const QString &confirm, const QString &code, const QString &name);
  Q_INVOKABLE void resetPassword(const QString &email, const QString &password,
                                 const QString &confirm, const QString &code, const QString &name);
  Q_INVOKABLE void loginUser(const QString &email, const QString &password);
  Q_INVOKABLE void disconnectChat();
  Q_INVOKABLE void reconnectNow();
  Q_INVOKABLE bool backgroundApplication();
  bool sessionActive() const
  {
    return m_session.active();
  }
  bool reconnecting() const
  {
    return m_session.recovering();
  }
  QString connectionText() const;
  MobileAccount *account()
  {
    return &m_account;
  }
  Q_INVOKABLE void startAutomaticLogin();
  Q_INVOKABLE void checkUpdates();
  Q_INVOKABLE void downloadUpdate();
  Q_INVOKABLE void cancelUpdate();
  Q_INVOKABLE void installUpdate();
  QVariantMap updateInfo() const;
  Q_INVOKABLE QVariantMap storageInfo() const;
  Q_INVOKABLE void searchHistory(int threadId, const QString &query, int type = -1,
                                 int beforeId = 2147483647,
                                 qint64 beforeLocal = 9223372036854775807LL);
  Q_INVOKABLE void clearLocalHistory(int threadId);
  Q_INVOKABLE void saveDraft(int threadId, const QString &text);
  Q_INVOKABLE void loadDraft(int threadId);
  Q_INVOKABLE void setPinned(int threadId, bool pinned);
  Q_INVOKABLE void updateProfile(const QString &nick, const QString &description);
  Q_INVOKABLE void deleteFriend(int uid);
  Q_INVOKABLE void groupCommand(const QString &operation, int threadId,
                                const QVariantMap &fields = QVariantMap());
  Q_INVOKABLE QVariantMap groupInfo(int threadId) const;
  Q_INVOKABLE QString newGroupCreationKey() const;
  Q_INVOKABLE bool sendTextMessage(int threadId, const QString &text);
  Q_INVOKABLE bool sendImage(int threadId, const QUrl &file);
  MobileMedia *media() const
  {
    return m_media;
  }
  QVariantMap mediaCapabilities() const;
  Q_INVOKABLE bool sendFile(int threadId, const QUrl &file);
  Q_INVOKABLE void startRecording(int threadId);
  Q_INVOKABLE bool sendVoice(int threadId);
  Q_INVOKABLE void startCall(int threadId, bool video);
  Q_INVOKABLE QVariantMap fileInfo(const QString &id) const;
  Q_INVOKABLE void downloadFile(const QString &id);
  Q_INVOKABLE void saveFile(const QString &id, const QUrl &destination);
  Q_INVOKABLE void retryMessage(int threadId, const QString &uniqueId);
  Q_INVOKABLE void loadChatThreads();
  Q_INVOKABLE void loadMessages(int threadId, int beforeId = 0, int limit = 50);
  Q_INVOKABLE void openChat(int uid);
  Q_INVOKABLE void searchUser(const QString &keyword);
  Q_INVOKABLE void addFriendApply(int uid, const QString &description);
  Q_INVOKABLE void acceptFriendApply(int uid);
  Q_INVOKABLE QVariantList friendList() const;
  Q_INVOKABLE QVariantList applyList() const;
  Q_INVOKABLE int currentUid() const;
  Q_INVOKABLE QVariantList chatThreads() const;
  Q_INVOKABLE QVariantMap threadInfo(int threadId) const;
  Q_INVOKABLE QVariantList messagesForThread(int threadId) const;
  Q_INVOKABLE QString avatarSource(int uid, const QString &fallback);
  Q_INVOKABLE QString imageSource(const QString &fileId);
  Q_INVOKABLE QString errorCodeMessage(int code) const;
  bool isLoggedIn() const;
  bool authBusy() const
  {
    return m_authRequest != 0;
  }
  bool storeReady() const;
  bool sending() const;
  QString currentUser() const;
  QString currentNick() const;
  QString currentDescription() const;

signals:
  void authBusyChanged();
  void sendingChanged();
  void verifyCodeResult(int error, const QString &message);
  void registerResult(int error, const QString &message);
  void resetResult(int error, const QString &message);
  void loginHttpResult(int error, const QString &message);
  void loginStatusChanged();
  void chatSwitched();
  void forceLogout(const QString &reason);
  void chatThreadsLoaded();
  void messagesLoaded(int threadId);
  void localHistoryLoaded(int threadId, bool more);
  void textMessageSaved(int threadId, const QString &text);
  void contactsChanged();
  void chatOpened(int threadId, const QString &name);
  void friendSearchResult(const QVariantMap &info);
  void friendApplySent(int uid);
  void avatarReady(int uid);
  void imageReady(const QString &fileId);
  void transferProgress(const QString &id, qint64 done, qint64 total, bool upload);
  void fileReady(const QString &id);
  void operationFailed(const QString &message);
  void statusMessage(const QString &message);
  void updateChanged();
  void historyResults(int threadId, QVariantList messages, bool more, int nextId, qint64 nextLocal);
  void historyCleared(int threadId);
  void draftLoaded(int threadId, const QString &text);
  void groupResult(QVariantMap response);

private:
  Q_SLOT void networkTransportChanged();
  void loadConfig();
  void connectBackends();
  void connectChatBackends();
  bool beginAuth(ReqId request);
  void finishAuth();
  void handleHttp(ReqId id, const QByteArray &data, ErrorCodes error);
  void resetSession();
  void initializeFeatures();
  void authenticatedAccount();
  void initializeMedia();
  void initializeRecovery();
  void restoreAccount();
  void startLogin(const QString &email, const QString &password);
  void loginFailed(int error, const QString &message);
  void connectionInterrupted();
  QString m_configPath;
  QString m_dataDirectory;
  QString m_gateUrl;
  ServerInfo m_loginServer;
  bool m_initialized = false;
  int m_authRequest = 0;
  int m_creatingUid = 0;
  ChatSyncService *m_sync = nullptr;
  MessageSender *m_sender = nullptr;
  MobileMedia *m_media = nullptr;
  QString m_attachmentKind;
  QSet<QString> m_requestedFiles;
  QByteArray m_savingVoice;
  std::shared_ptr<SearchInfo> m_searchResult;
  std::shared_ptr<PreparedAttachment> m_pendingImage;
  std::shared_ptr<ChatThreadData> m_imageThread;
  QString m_pendingText;
  int m_pendingTextThread = 0;
  MobileAccount m_account{this};
  MobileSession m_session{this};
  ClientUpdateService *m_updates = nullptr;
  QString m_loginEmail, m_loginPassword;
  QString m_updateMessage;
  bool m_manualUpdate = false, m_autoStarted = false;
};

#endif // APPBRIDGE_H
