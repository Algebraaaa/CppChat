#ifndef CHATDIALOG_H
#define CHATDIALOG_H
#include "common/global.h"
#include "common/userdata.h"
#include <QDialog>
#include <QQueue>
#include <QSet>
class FriendInfoPage;
class UserInfoPage;
class QListWidgetItem;
namespace Ui { class ChatDialog; }

class ChatDialog : public QDialog {
  Q_OBJECT
public:
  explicit ChatDialog(QWidget *parent = nullptr);
  ~ChatDialog() override;
  void StartSession();
  void ResetSession();
signals:
  void sig_logout();
protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
private:
  void ShowSearch(bool search);
  void ShowChatPage();
  void ShowContactPage();
  void RefreshAvatar();
  void RefreshContacts();
  void LoadThreads();
  void AddChatUserList(int limit = CHAT_COUNT_PER_PAGE);
  void UpdateChatItem(int threadId, bool addIfMissing = true);
  void OpenChat(std::shared_ptr<UserInfo> user);
  void SelectThread(int threadId);
  std::shared_ptr<ChatThreadData> EnsureThread(int threadId, int otherUid);
  void QueueHistory(int threadId);
  void LoadNextHistory();
  void SendHistoryRequest();
  void ReceiveThreads(bool more, int lastId, std::vector<std::shared_ptr<ChatThreadInfo>> threads);
  void ReceiveHistory(int threadId, int messageId, bool more, std::vector<std::shared_ptr<TextChatData>> messages);
  void ReceiveMessages(const std::vector<std::shared_ptr<TextChatData>> &messages, bool reply);
  void FriendAccepted(int uid, int threadId, const std::vector<std::shared_ptr<TextChatData>> &messages);
  void RequestFailed(ReqId request, const QString &message);
  Ui::ChatDialog *ui;
  FriendInfoPage *_friendInfoPage = nullptr;
  UserInfoPage *_userInfoPage = nullptr;
  ChatUIMode _state = ChatMode;
  QMap<int, QListWidgetItem *> _threadItems;
  QSet<int> _unreadThreads;
  QQueue<int> _historyQueue;
  QSet<int> _historyComplete;
  int _currentThread = 0;
  int _threadCursor = 0;
  int _historyThread = 0;
  int _historyCursor = 0;
  int _creatingUser = 0;
  bool _threadsLoading = false;
  bool _threadsComplete = false;
  bool _addingItems = false;
};
#endif
