#include "searchlist.h"

#include "dialogs/chatuserwid.h"
#include "dialogs/grouptipitem.h"
#include "network/usermgr.h"

#include <QLabel>

namespace {
constexpr int kResultKindRole = Qt::UserRole;
constexpr int kUserIdRole = Qt::UserRole + 1;
constexpr int kThreadIdRole = Qt::UserRole + 2;

bool containsKeyword(const QString &text, const QString &keyword)
{
  return text.contains(keyword, Qt::CaseInsensitive);
}

bool userMatches(const std::shared_ptr<UserInfo> &user, const QString &keyword)
{
  return user && (containsKeyword(QString::number(user->_uid), keyword) ||
                  containsKeyword(user->_name, keyword) || containsKeyword(user->_nick, keyword) ||
                  containsKeyword(user->_back, keyword) || containsKeyword(user->_desc, keyword));
}
} // namespace

SearchList::SearchList(QWidget *parent) : QListWidget(parent)
{
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  setSelectionMode(QAbstractItemView::SingleSelection);
  connect(this, &QListWidget::itemClicked, this, &SearchList::slotItemClicked);
}

void SearchList::Search(const QString &keyword, Scope scope)
{
  ClearResults();

  const QString query = keyword.trimmed();
  if (query.isEmpty())
    return;

  const auto userManager = UserMgr::GetInstance();
  int resultCount = 0;

  for (const auto &user : userManager->GetFriendList()) {
    if (!userMatches(user, query))
      continue;

    if (resultCount == 0)
      addSection(tr("联系人"));
    addResult(ResultKind::Friend, user, user->_desc);
    ++resultCount;
  }

  bool chatSectionAdded = false;
  for (const auto &thread : userManager->GetChatThreads()) {
    if (scope == Scope::Contacts)
      break;
    if (!thread)
      continue;

    const auto user = userManager->GetFriendById(thread->GetOtherId());
    if (!user)
      continue;

    QString matchedPreview;

    // 名字或会话摘要命中时，展示这段会话的最新摘要。
    if (userMatches(user, query) || containsKeyword(thread->GetLastMsg(), query))
      matchedPreview = thread->GetLastMsg();

    // 从最近的已加载文本消息向前查找。每个会话只显示一个最相关结果，
    // 避免同一个联系人因为多条命中消息占满整个搜索列表。
    if (matchedPreview.isEmpty()) {
      const auto &messages = thread->GetMsgMapRef();
      for (auto it = messages.cend(); it != messages.cbegin();) {
        --it;
        const auto &message = it.value();
        if (message && message->GetMsgType() == ChatMsgType::TEXT &&
            containsKeyword(message->GetContent(), query)) {
          matchedPreview = message->GetContent();
          break;
        }
      }
    }

    if (matchedPreview.isEmpty()) {
      const auto pendingMessages = thread->GetPendingMessages();
      for (auto it = pendingMessages.crbegin(); it != pendingMessages.crend(); ++it) {
        const auto &message = *it;
        if (message && message->GetMsgType() == ChatMsgType::TEXT &&
            containsKeyword(message->GetContent(), query)) {
          matchedPreview = message->GetContent();
          break;
        }
      }
    }

    // 名字命中了但会话尚无消息时，仍然允许用户从“聊天记录”进入该会话。
    if (matchedPreview.isEmpty() && userMatches(user, query))
      matchedPreview = tr("已有会话");

    if (matchedPreview.isEmpty())
      continue;

    if (!chatSectionAdded) {
      addSection(tr("聊天记录"));
      chatSectionAdded = true;
    }
    addResult(ResultKind::ChatRecord, user, matchedPreview, thread->GetThreadId());
    ++resultCount;
  }

  if (resultCount == 0)
    addEmptyTip(scope);
}

void SearchList::ClearResults()
{
  clear();
}

void SearchList::addSection(const QString &title)
{
  auto *item = new QListWidgetItem(this);
  auto *section = new GroupTipItem(this);
  section->SetGroupTip(title);
  section->setAttribute(Qt::WA_TransparentForMouseEvents);
  item->setSizeHint(section->sizeHint());
  item->setFlags(Qt::NoItemFlags);
  setItemWidget(item, section);
}

void SearchList::addResult(ResultKind kind, const std::shared_ptr<UserInfo> &user,
                           const QString &preview, int threadId)
{
  if (!user)
    return;

  auto *item = new QListWidgetItem(this);
  auto *resultWidget = new ChatUserWid(this);
  resultWidget->SetSearchInfo(user, preview);
  item->setSizeHint(resultWidget->sizeHint());
  item->setData(kResultKindRole, static_cast<int>(kind));
  item->setData(kUserIdRole, user->_uid);
  item->setData(kThreadIdRole, threadId);
  setItemWidget(item, resultWidget);
}

void SearchList::addEmptyTip(Scope scope)
{
  auto *item = new QListWidgetItem(this);
  item->setSizeHint(QSize(1, 64));
  item->setFlags(Qt::NoItemFlags);

  auto *label = new QLabel(scope == Scope::Contacts ? tr("没有找到匹配的联系人")
                                                    : tr("没有找到匹配的联系人或聊天记录"),
                           this);
  label->setObjectName("search_empty_tip");
  label->setAlignment(Qt::AlignCenter);
  setItemWidget(item, label);
}

void SearchList::slotItemClicked(QListWidgetItem *item)
{
  if (!item)
    return;

  const auto kind = static_cast<ResultKind>(item->data(kResultKindRole).toInt());
  if (kind == ResultKind::Friend) {
    const auto user = UserMgr::GetInstance()->GetFriendById(item->data(kUserIdRole).toInt());
    if (user)
      emit sig_open_friend(user);
    return;
  }

  if (kind == ResultKind::ChatRecord) {
    const int threadId = item->data(kThreadIdRole).toInt();
    if (threadId > 0)
      emit sig_open_thread(threadId);
  }
}
