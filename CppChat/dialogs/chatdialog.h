#ifndef CHATDIALOG_H
#define CHATDIALOG_H
#include "common/global.h"
#include "common/userdata.h"
#include <QDialog>
#include <QPointer>
#include <QSet>
class AddFriendDialog;
class ProfileDialog;
class SideMenuPopup;
class ChatSyncService;
class MessageForwarder;
class ReminderService;
class RemindersDialog;
class NotesDialog;
class ChatHistoryDialog;
class ClientSettingsDialog;
namespace Ui {
class ChatDialog;
}

class ChatDialog : public QDialog {
  Q_OBJECT
public:
  explicit ChatDialog(QWidget *parent = nullptr);
  ~ChatDialog() override;
  void StartSession();
  void ResetSession();
signals:
  void sig_logout();
  void checkUpdatesRequested();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  enum class Section { Chat, Contacts, Favorites, Discover };
  void SwitchSection(Section section);
  bool IsChatVisible() const;
  void MarkCurrentRead();
  void SetThreadUnread(int threadId, bool unread, bool manuallyMarked = false);
  void ShowChatPage();
  void ShowContactPage();
  void ShowFavoritesPage();
  void ShowDiscoverPage();
  void RefreshAvatar();
  void RefreshContacts();
  void RefreshConversationList(bool complete);
  void AddChatUserList(int limit = CHAT_COUNT_PER_PAGE);
  void UpdateChatItem(int threadId, bool addIfMissing = true);
  void OpenChat(std::shared_ptr<UserInfo> user);
  void SelectThread(int threadId);
  void FriendAccepted(int uid, int threadId,
                      const std::vector<std::shared_ptr<TextChatData>> &messages);
  void FriendDeleted(int uid);
  void RequestFailed(ReqId request, const QString &message);
  void ShowNotes();
  void ShowReminders();
  void ShowHistory();
  void ShowSettings();
  Ui::ChatDialog *ui;
  ChatSyncService *_sync = nullptr;
  MessageForwarder *_forwarder = nullptr;
  ReminderService *_reminders = nullptr;
  QPointer<RemindersDialog> _remindersDialog;
  QPointer<NotesDialog> _notesDialog;
  QPointer<ChatHistoryDialog> _globalHistory;
  QPointer<ClientSettingsDialog> _settingsDialog;
  ProfileDialog *_profileDialog = nullptr;
  SideMenuPopup *_sideMenu = nullptr;
  QPointer<AddFriendDialog> _addFriendDialog;
  Section _currentSection = Section::Chat;
  quint64 _navigationRevision = 0;
  quint64 _createNavigationRevision = 0;
  QSet<int> _unreadThreads;
  QSet<int> _manuallyUnreadThreads;
  int _currentThread = 0;
  int _creatingUser = 0;
};
#endif
