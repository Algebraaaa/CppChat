#include "conversationsection.h"
#include "chatpage.h"
#include "chatuserwid.h"
#include "common/conversationpreferences.h"
#include "conversationmenu.h"
#include "network/usermgr.h"
#include "ui_conversationsection.h"
#include <QAction>
#include <QApplication>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <algorithm>
#include <vector>

namespace {
enum ThreadRole {
  ThreadIdRole = Qt::UserRole,
  LastChatTimeRole,
  LastMessageIdRole,
  PinnedRole,
  UnreadRole
};

class ChatThreadItem : public QListWidgetItem {
public:
  bool operator<(const QListWidgetItem &other) const override
  {
    const bool pinned = data(PinnedRole).toBool();
    const bool otherPinned = other.data(PinnedRole).toBool();
    if (pinned != otherPinned)
      return !pinned;
    const QString time = data(LastChatTimeRole).toString();
    const QString otherTime = other.data(LastChatTimeRole).toString();
    if (time != otherTime)
      return time < otherTime;
    const int messageId = data(LastMessageIdRole).toInt();
    const int otherMessageId = other.data(LastMessageIdRole).toInt();
    return messageId != otherMessageId
               ? messageId < otherMessageId
               : data(ThreadIdRole).toInt() < other.data(ThreadIdRole).toInt();
  }
};
} // namespace

ConversationSection::ConversationSection(QWidget *parent)
    : QWidget(parent), ui(new Ui::ConversationSection)
{
  ui->setupUi(this);
  setAttribute(Qt::WA_StyledBackground);
  ui->chat_user_wid->setAttribute(Qt::WA_StyledBackground);
  ui->main_splitter->setStretchFactor(0, 0);
  ui->main_splitter->setStretchFactor(1, 1);
  ui->main_splitter->setSizes({300, 820});
  ui->search_edit->SetMaxLength(27);
  ui->search_edit->setPlaceholderText(tr("搜索"));
  ui->search_edit->setToolTip(tr("搜索已添加的联系人和聊天记录"));
  ui->search_edit->addAction(QIcon(":/res/search.png"), QLineEdit::LeadingPosition);
  auto *clear =
      ui->search_edit->addAction(QIcon(":/res/close_search.png"), QLineEdit::TrailingPosition);
  clear->setVisible(false);
  _sortTimer.setSingleShot(true);
  _sortTimer.setInterval(0);
  connect(&_sortTimer, &QTimer::timeout, this, &ConversationSection::FlushSort);
  _searchTimer.setSingleShot(true);
  connect(&_searchTimer, &QTimer::timeout, this, [this] {
    if (!HasSearch())
      return;
    ui->search_list->Search(ui->search_edit->text());
    if (_searchHistoryRequested) {
      _searchHistoryRequested = false;
      emit sig_search_history_requested();
    }
  });
  connect(clear, &QAction::triggered, this, &ConversationSection::ClearSearch);
  connect(ui->search_edit, &QLineEdit::textChanged, this, [this, clear](const QString &text) {
    clear->setVisible(!text.isEmpty());
    // 立即移除旧关键词的结果，避免防抖等待期间点进错误的会话。
    _searchTimer.stop();
    ui->search_list->ClearResults();
    _searchHistoryRequested = HasSearch();
    ShowSearch(HasSearch());
    if (HasSearch())
      _searchTimer.start(200);
  });
  connect(ui->search_list, &SearchList::sig_open_friend, this,
          &ConversationSection::sig_open_friend);
  connect(ui->search_list, &SearchList::sig_open_thread, this,
          &ConversationSection::sig_open_thread);
  connect(ui->add_btn, &QPushButton::clicked, this,
          &ConversationSection::sig_quick_actions_requested);
  connect(ui->chat_user_list, &ChatUserList::sig_loading_chat_user, this, [this] {
    if (!_addingItems)
      emit sig_load_more();
  });
  connect(ui->chat_user_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
    emit sig_open_thread(item->data(ThreadIdRole).toInt());
  });
  _contextMenu = new ConversationMenu(this);
  connect(ui->chat_user_list, &ChatUserList::sig_context_menu_requested, this,
          [this](QListWidgetItem *item, const QPoint &position) {
            _contextMenu->Popup(item->data(ThreadIdRole).toInt(), item->data(PinnedRole).toBool(),
                                item->data(UnreadRole).toBool(), position);
          });
  connect(_contextMenu, &ConversationMenu::sig_pin_changed, this,
          &ConversationSection::SetThreadPinned);
  connect(ui->chat_page, &ChatPage::sig_thread_pin_changed, this,
          &ConversationSection::SetThreadPinned);
  connect(_contextMenu, &ConversationMenu::sig_unread_changed, this,
          &ConversationSection::sig_thread_unread_changed);
  connect(_contextMenu, &ConversationMenu::sig_placeholder, this, [this](const QString &title) {
    ui->chat_page->ShowStatus(tr("%1功能暂未实现").arg(title));
  });
  connect(ui->chat_user_list, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem *current, QListWidgetItem *previous) {
            for (auto *item : {previous, current}) {
              if (item) {
                auto *widget = qobject_cast<ChatUserWid *>(ui->chat_user_list->itemWidget(item));
                if (widget)
                  widget->SetSelected(item == current);
              }
            }
          });
  ShowSearch(false);
  qApp->installEventFilter(this);
}

ConversationSection::~ConversationSection()
{
  qApp->removeEventFilter(this);
  delete ui;
}

ChatPage *ConversationSection::Chat() const { return ui->chat_page; }
bool ConversationSection::HasSearch() const { return !ui->search_edit->text().trimmed().isEmpty(); }
void ConversationSection::ClearSearch() { ui->search_edit->clear(); }
void ConversationSection::RefreshSearch()
{
  // 数据更新只合并刷新，不打断正在等待的输入防抖。
  if (HasSearch() && !_searchTimer.isActive())
    _searchTimer.start(0);
}

void ConversationSection::ScheduleSort()
{
  _sortDirty = true;
  if (!_addingItems && !_sortTimer.isActive())
    _sortTimer.start();
}

void ConversationSection::FlushSort()
{
  _sortTimer.stop();
  if (!_sortDirty)
    return;
  _sortDirty = false;
  const bool wasAdding = _addingItems;
  _addingItems = true;
  ui->chat_user_list->sortItems(Qt::DescendingOrder);
  _addingItems = wasAdding;
}

void ConversationSection::ShowSearch(bool show)
{
  ui->search_list->setVisible(show);
  ui->chat_user_list->setVisible(!show);
}

QPoint ConversationSection::QuickMenuPosition(const QSize &menuSize) const
{
  return ui->add_btn->mapToGlobal(
      QPoint(ui->add_btn->width() - menuSize.width() + 12, ui->add_btn->height() - 4));
}

void ConversationSection::Reset()
{
  _sortTimer.stop();
  _searchTimer.stop();
  _sortDirty = false;
  _searchHistoryRequested = false;
  _contextMenu->hide();
  _pinnedThreads.clear();
  ClearSearch();
  ui->search_list->ClearResults();
  _threadItems.clear();
  ui->chat_user_list->clear();
  ui->chat_page->Reset();
  _addingItems = false;
  ShowSearch(false);
}

void ConversationSection::SetPinnedThreads(const QSet<int> &threads)
{
  const bool wasAdding = _addingItems;
  _addingItems = true;
  _pinnedThreads = threads;
  for (auto it = _threadItems.cbegin(); it != _threadItems.cend(); ++it) {
    if (it.value()->data(PinnedRole).toBool() != threads.contains(it.key()))
      ScheduleSort();
    it.value()->setData(PinnedRole, threads.contains(it.key()));
    ui->chat_page->SetThreadPinned(it.key(), threads.contains(it.key()));
    if (auto *widget = qobject_cast<ChatUserWid *>(ui->chat_user_list->itemWidget(it.value())))
      widget->SetPinned(threads.contains(it.key()));
  }
  _addingItems = wasAdding;
  FlushSort();
}

void ConversationSection::SetThreadPinned(int threadId, bool pinned)
{
  if (!_threadItems.contains(threadId))
    return;
  if (_pinnedThreads.contains(threadId) == pinned)
    return;
  auto threads = _pinnedThreads;
  if (pinned)
    threads.insert(threadId);
  else
    threads.remove(threadId);
  SetPinnedThreads(threads);
  ConversationPreferences::SavePinned(UserMgr::GetInstance()->GetUid(), threads);
}

void ConversationSection::AddThreads(const QSet<int> &unread, int limit)
{
  if (_addingItems || limit <= 0)
    return;
  _addingItems = true;
  std::vector<std::shared_ptr<ChatThreadData>> threads;
  for (const auto &data : UserMgr::GetInstance()->GetChatThreads())
    if (data)
      threads.push_back(data);
  std::sort(threads.begin(), threads.end(), [this](const auto &left, const auto &right) {
    const bool leftPinned = _pinnedThreads.contains(left->GetThreadId());
    const bool rightPinned = _pinnedThreads.contains(right->GetThreadId());
    if (leftPinned != rightPinned)
      return leftPinned;
    if (left->GetLastChatTime() != right->GetLastChatTime())
      return left->GetLastChatTime() > right->GetLastChatTime();
    if (left->GetLastMsgId() != right->GetLastMsgId())
      return left->GetLastMsgId() > right->GetLastMsgId();
    return left->GetThreadId() > right->GetThreadId();
  });
  int count = 0;
  for (const auto &data : threads) {
    if (!data || _threadItems.contains(data->GetThreadId()))
      continue;
    UpdateThread(data, unread.contains(data->GetThreadId()));
    if (++count >= limit)
      break;
  }
  _addingItems = false;
  FlushSort();
}

void ConversationSection::UpdateThread(std::shared_ptr<ChatThreadData> data, bool unread,
                                       bool addIfMissing)
{
  if (!data)
    return;
  const int threadId = data->GetThreadId();
  auto *item = _threadItems.value(threadId, nullptr);
  if (!item && !addIfMissing)
    return;
  const bool wasAdding = _addingItems;
  _addingItems = true;
  if (!item) {
    ScheduleSort();
    auto *widget = new ChatUserWid;
    item = new ChatThreadItem;
    item->setData(ThreadIdRole, threadId);
    item->setSizeHint(widget->sizeHint());
    _threadItems.insert(threadId, item);
    ui->chat_user_list->addItem(item);
    ui->chat_user_list->setItemWidget(item, widget);
  }
  auto *widget = qobject_cast<ChatUserWid *>(ui->chat_user_list->itemWidget(item));
  widget->SetChatData(data);
  widget->ShowRedPoint(unread);
  widget->SetPinned(_pinnedThreads.contains(threadId));
  if (item->data(LastChatTimeRole).toString() != data->GetLastChatTime() ||
      item->data(LastMessageIdRole).toInt() != data->GetLastMsgId() ||
      item->data(PinnedRole).toBool() != _pinnedThreads.contains(threadId))
    ScheduleSort();
  item->setData(LastChatTimeRole, data->GetLastChatTime());
  item->setData(LastMessageIdRole, data->GetLastMsgId());
  item->setData(PinnedRole, _pinnedThreads.contains(threadId));
  item->setData(UnreadRole, unread);
  item->setToolTip(data->GetLastChatTime());
  _addingItems = wasAdding;
  if (_sortDirty && !_addingItems && !_sortTimer.isActive())
    _sortTimer.start();
  RefreshSearch();
}

void ConversationSection::SelectThread(std::shared_ptr<ChatThreadData> data)
{
  ui->chat_user_list->setCurrentItem(_threadItems.value(data->GetThreadId()));
  ui->chat_page->SetChatData(data);
  ui->chat_page->SetThreadPinned(data->GetThreadId(), _pinnedThreads.contains(data->GetThreadId()));
  ui->chat_page->ShowStatus({});
  ClearSearch();
}

void ConversationSection::RemoveThread(int threadId)
{
  _contextMenu->hide();
  _pinnedThreads.remove(threadId);
  ConversationPreferences::RemoveThread(UserMgr::GetInstance()->GetUid(), threadId);
  if (auto *item = _threadItems.take(threadId))
    delete ui->chat_user_list->takeItem(ui->chat_user_list->row(item));
  ui->chat_page->RemoveChatThread(threadId);
  RefreshSearch();
}

int ConversationSection::ThreadCount() const { return ui->chat_user_list->count(); }
int ConversationSection::FirstThreadId()
{
  // 登录后选择第一条会话前，必须应用本轮更新的时间和置顶排序。
  FlushSort();
  return ThreadCount() > 0 ? ui->chat_user_list->item(0)->data(ThreadIdRole).toInt() : 0;
}

bool ConversationSection::eventFilter(QObject *watched, QEvent *event)
{
  if (event->type() == QEvent::MouseButtonPress && isVisible() && ui->search_list->isVisible()) {
    const auto *widget = qobject_cast<QWidget *>(watched);
    if (widget && isAncestorOf(widget) && widget->window() == window()) {
      const QPoint pos = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
      if (!ui->search_edit->rect().contains(ui->search_edit->mapFromGlobal(pos)) &&
          !ui->search_list->rect().contains(ui->search_list->mapFromGlobal(pos)) &&
          !ui->add_btn->rect().contains(ui->add_btn->mapFromGlobal(pos)))
        ClearSearch();
    }
  }
  return QWidget::eventFilter(watched, event);
}
