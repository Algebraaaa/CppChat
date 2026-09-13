#include "contactuserlist.h"
#include "dialogs/conuseritem.h"
#include "dialogs/grouptipitem.h"
#include "network/usermgr.h"

ContactUserList::ContactUserList(QWidget *parent) : SmoothScrollList(parent)
{
  connect(this, &QListWidget::itemClicked, this, &ContactUserList::slot_item_clicked);
}
void ContactUserList::Reload()
{
  _loading = true;
  clear();
  _add_friend_item = nullptr;
  _loadedCount = 0;
  auto addGroup = [this](const QString &title) {
    auto *widget = new GroupTipItem;
    widget->SetGroupTip(title);
    widget->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *item = new QListWidgetItem(this);
    item->setSizeHint(widget->sizeHint());
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    setItemWidget(item, widget);
  };
  addGroup(tr("新的朋友"));
  _add_friend_item = new ConUserItem;
  _add_friend_item->setObjectName("new_friend_item");
  _add_friend_item->SetInfo(0, tr("新的朋友"), ":/res/add_friend.png");
  _add_friend_item->SetItemType(ListItemType::APPLY_FRIEND_ITEM);
  _add_friend_item->setAttribute(Qt::WA_TransparentForMouseEvents);
  auto *newFriend = new QListWidgetItem(this);
  newFriend->setSizeHint(_add_friend_item->sizeHint());
  setItemWidget(newFriend, _add_friend_item);
  addGroup(tr("联系人"));
  _loading = false;
  LoadMore();
}
void ContactUserList::LoadMore()
{
  if (_loading) return;
  _loading = true;
  const auto &friends = UserMgr::GetInstance()->GetFriendList();
  const int end = qMin(_loadedCount + CHAT_COUNT_PER_PAGE, static_cast<int>(friends.size()));
  while (_loadedCount < end) {
    auto *widget = new ConUserItem;
    widget->SetInfo(friends[_loadedCount++]);
    widget->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *item = new QListWidgetItem(this);
    item->setSizeHint(widget->sizeHint());
    setItemWidget(item, widget);
  }
  _loading = false;
}
void ContactUserList::ShowRedPoint(bool show)
{ if (_add_friend_item) _add_friend_item->ShowRedPoint(show); }
void ContactUserList::onReachedBottom()
{ if (!_loading) emit sig_loading_contact_user(); }
void ContactUserList::slot_item_clicked(QListWidgetItem *item)
{
  auto *widget = qobject_cast<ConUserItem *>(itemWidget(item));
  if (!widget) return;
  if (widget->GetItemType() == APPLY_FRIEND_ITEM) emit sig_switch_apply_friend_page();
  else if (widget->GetItemType() == CONTACT_USER_ITEM) emit sig_switch_friend_info_page(widget->GetInfo());
}
