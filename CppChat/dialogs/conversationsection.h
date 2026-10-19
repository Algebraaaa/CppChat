#ifndef CONVERSATIONSECTION_H
#define CONVERSATIONSECTION_H

#include "common/userdata.h"
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QWidget>

class ChatPage;
class ChatWindow;
class ConversationMenu;
class QListWidgetItem;
namespace Ui {
class ConversationSection;
}

// 完整聊天模块：搜索、会话列表和会话内容。网络分页与跨模块导航由 ChatDialog 协调。
class ConversationSection : public QWidget {
  Q_OBJECT
public:
  explicit ConversationSection(QWidget *parent = nullptr);
  ~ConversationSection() override;
  ChatPage *Chat() const;
  void OpenChatWindow();
  void DockChat();
  void Reset();
  void SetPinnedThreads(const QSet<int> &threads);
  void SetThreadPinned(int threadId, bool pinned);
  void LoadLocalState();
  void SetThreadMuted(int threadId, bool muted);
  bool IsMuted(int threadId) const;
  bool IsHidden(int threadId) const;
  bool RevealThread(int threadId);
  void HideThread(int threadId);
  void DeleteLocalThread(int threadId);
  bool HasAlert(const QSet<int> &unread) const;
  void RefreshSearch();
  void ClearSearch();
  bool HasSearch() const;
  QPoint QuickMenuPosition(const QSize &menuSize) const;
  void AddThreads(const QSet<int> &unread, int limit);
  void UpdateThread(std::shared_ptr<ChatThreadData> data, bool unread, bool addIfMissing = true);
  void SelectThread(std::shared_ptr<ChatThreadData> data);
  void RemoveThread(int threadId);
  int ThreadCount() const;
  int FirstThreadId();

signals:
  void sig_open_friend(std::shared_ptr<UserInfo> user);
  void sig_open_thread(int threadId);
  void sig_window_requested(int threadId);
  void sig_load_more();
  void sig_search_history_requested();
  void sig_quick_actions_requested();
  void sig_thread_unread_changed(int threadId, bool unread);
  void sig_thread_hidden(int threadId);
  void sig_alert_preferences_changed();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void ShowSearch(bool show);
  void ScheduleSort();
  void FlushSort();
  bool HideAt(int threadId, int boundary);
  void RemoveVisibleThread(int threadId);
  Ui::ConversationSection *ui;
  QMap<int, QListWidgetItem *> _threadItems;
  QSet<int> _pinnedThreads;
  QSet<int> _mutedThreads;
  QHash<int, int> _hiddenThreads;
  QSet<int> _deleting;
  ConversationMenu *_contextMenu = nullptr;
  ChatWindow *_chatWindow = nullptr;
  QWidget *_detachedPlaceholder = nullptr;
  QList<int> _splitterSizes;
  bool _addingItems = false;
  QTimer _sortTimer;
  QTimer _searchTimer;
  bool _sortDirty = false;
  bool _searchHistoryRequested = false;
};
#endif
