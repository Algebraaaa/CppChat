#include "contactuserlist.h"
#include "network/usermgr.h"
#include <QCollator>
#include <QPainter>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <algorithm>

namespace {
enum EntryKind
{
  NewFriends,
  Application,
  Category,
  ContactsHeader,
  Starred,
  Alphabet,
  Friend,
  Empty
};
enum EntryRole
{
  KindRole = Qt::UserRole,
  UidRole,
  IconRole,
  ExpandedRole,
  UnreadRole,
  KeyRole,
  DescriptionRole,
  StatusRole
};

QString contactName(const std::shared_ptr<UserInfo> &user)
{
  return user->_back.trimmed().isEmpty() ? user->_name : user->_back;
}

QString contactInitial(const QString &name)
{
  const QChar first = name.trimmed().isEmpty() ? QChar() : name.trimmed().front().toUpper();
  return first >= QLatin1Char('A') && first <= QLatin1Char('Z') ? QString(first) : QString("#");
}

// 委托只绘制可见行，避免每个联系人都创建 QWidget；整行选中、缩略头像和省略文字统一绘制。
class ContactDelegate : public QStyledItemDelegate
{
public:
  explicit ContactDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override
  {
    QStyleOptionViewItem style(option);
    initStyleOption(&style, index);
    const int kind = index.data(KindRole).toInt();
    const bool selected = style.state.testFlag(QStyle::State_Selected);
    const QColor muted = option.widget->property("secondaryColor").value<QColor>();
    style.text.clear();
    style.icon = {};
    style.widget->style()->drawControl(QStyle::CE_ItemViewItem, &style, painter, style.widget);
    painter->save();
    if (kind == Application && selected)
      painter->fillRect(option.rect,
                        option.widget->property("applicationSelectionColor").value<QColor>());
    painter->setFont(option.font);
    const QRect area = option.rect;
    const QString title = index.data(Qt::DisplayRole).toString();
    painter->setPen(selected ? option.palette.highlightedText().color()
                             : option.palette.text().color());

    if (kind == Application) {
      const int uid = index.data(UidRole).toInt();
      const QRect avatar(area.left() + 42, area.center().y() - 20, 40, 40);
      const auto image = UserMgr::GetInstance()->AvatarThumbnail(
          uid, index.data(IconRole).toString(), avatar.size(),
          painter->device()->devicePixelRatioF());
      painter->drawPixmap(avatar.topLeft(), image);

      const QString status = index.data(StatusRole).toInt() == 0 ? tr("待接受") : tr("已添加");
      const int statusWidth = option.fontMetrics.horizontalAdvance(status);
      const int textLeft = area.left() + 96;
      const int textRight = area.right() - 16;
      const QRect name(textLeft, area.top() + 15, qMax(0, textRight - statusWidth - 10 - textLeft),
                       26);
      const QRect description(textLeft, area.top() + 43, qMax(0, textRight - textLeft), 24);
      const QRect statusArea(textRight - statusWidth, name.top(), statusWidth, name.height());
      painter->setPen(option.palette.text().color());
      painter->drawText(name, Qt::AlignVCenter | Qt::AlignLeft,
                        option.fontMetrics.elidedText(title, Qt::ElideRight, name.width()));
      painter->setPen(muted);
      painter->drawText(statusArea, Qt::AlignVCenter | Qt::AlignRight, status);
      painter->drawText(description, Qt::AlignVCenter | Qt::AlignLeft,
                        option.fontMetrics.elidedText(index.data(DescriptionRole).toString(),
                                                      Qt::ElideRight, description.width()));
    } else if (kind == Friend) {
      const int uid = index.data(UidRole).toInt();
      const QRect avatar(area.left() + 42, area.center().y() - 18, 36, 36);
      const auto image = UserMgr::GetInstance()->AvatarThumbnail(
          uid, index.data(IconRole).toString(), avatar.size(),
          painter->device()->devicePixelRatioF());
      painter->drawPixmap(avatar.topLeft(), image);
      const QRect text(area.left() + 90, area.top(), qMax(0, area.width() - 104), area.height());
      painter->drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                        option.fontMetrics.elidedText(title, Qt::ElideRight, text.width()));
    } else if (kind == Alphabet) {
      painter->setPen(muted);
      painter->drawText(area.adjusted(42, 0, -14, 0), Qt::AlignVCenter, title);
      painter->setPen(option.widget->property("dividerColor").value<QColor>());
      painter->drawLine(area.left() + 42, area.bottom(), area.right() - 14, area.bottom());
    } else if (kind == Starred || kind == Empty) {
      painter->setPen(muted);
      if (kind == Starred)
        _star.paint(painter, QRect(area.left() + 42, area.center().y() - 9, 18, 18));
      painter->drawText(area.adjusted(kind == Starred ? 68 : 42, 0, -14, 0), Qt::AlignVCenter,
                        title);
    } else {
      const auto &arrow =
          (kind == ContactsHeader || kind == NewFriends) && index.data(ExpandedRole).toBool()
              ? _chevronDown
              : _chevronRight;
      arrow.paint(painter, QRect(area.left() + 20, area.center().y() - 9, 18, 18));
      QString count;
      if (kind == ContactsHeader)
        count = index.data(UidRole).toString();
      const int countWidth = count.isEmpty() ? 0 : option.fontMetrics.horizontalAdvance(count) + 12;
      const QRect text = area.adjusted(44, 0, -16 - countWidth, 0);
      painter->drawText(text, Qt::AlignVCenter,
                        option.fontMetrics.elidedText(title, Qt::ElideRight, text.width()));
      painter->setPen(muted);
      painter->drawText(area.adjusted(0, 0, -16, 0), Qt::AlignVCenter | Qt::AlignRight, count);
      if (kind == NewFriends && index.data(UnreadRole).toBool()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor("#fa5151"));
        painter->drawEllipse(QPoint(area.right() - 21, area.center().y()), 4, 4);
      }
    }
    painter->restore();
  }

private:
  const QIcon _star{":/icons/contact-star.svg"};
  const QIcon _chevronDown{":/icons/contact-chevron-down.svg"};
  const QIcon _chevronRight{":/icons/contact-chevron-right.svg"};
};
} // namespace

ContactUserList::ContactUserList(QWidget *parent) : SmoothScrollList(parent)
{
  setItemDelegate(new ContactDelegate(this));
  setSpacing(0);
  connect(this, &QListWidget::itemClicked, this, &ContactUserList::slot_item_clicked);
  connect(UserMgr::GetInstance().get(), &UserMgr::sig_avatar_ready, this, [this](int uid) {
    for (int i = 0; i < count(); ++i)
      if ((item(i)->data(KindRole).toInt() == Friend ||
           item(i)->data(KindRole).toInt() == Application) &&
          item(i)->data(UidRole).toInt() == uid)
        viewport()->update(visualItemRect(item(i)));
  });
}

void ContactUserList::Reload(bool preserveSelection)
{
  const QString selectedKey =
      preserveSelection && currentItem() ? currentItem()->data(KeyRole).toString() : QString();
  const int scroll = preserveSelection ? verticalScrollBar()->value() : 0;
  if (!preserveSelection) {
    _contactsExpanded = true;
    _applicationsExpanded = true;
    _redPoint = false;
  }
  clear();
  _contactRows.clear();
  _applicationRows.clear();
  const auto addRow = [this](const QString &title, EntryKind kind, const QString &key, int height) {
    auto *row = new QListWidgetItem(title, this);
    row->setData(KindRole, kind);
    row->setData(KeyRole, key);
    row->setSizeHint(QSize(0, height));
    row->setToolTip(title);
    if (kind == Alphabet || kind == ContactsHeader || kind == NewFriends || kind == Empty)
      row->setFlags(Qt::ItemIsEnabled);
    return row;
  };
  _newFriends = addRow(tr("新的朋友"), NewFriends, "new-friends", 44);
  _newFriends->setData(UnreadRole, _redPoint);
  // 与联系人共用一份列表和滚动条；动态行不嵌套另一个列表控件。
  const auto &applications = UserMgr::GetInstance()->GetApplyList();
  for (auto it = applications.rbegin(); it != applications.rend(); ++it) {
    const auto &apply = *it;
    if (!apply || apply->_uid <= 0)
      continue;
    const QString name = apply->_nick.trimmed().isEmpty() ? apply->_name : apply->_nick;
    auto *row = addRow(name, Application, "apply-" + QString::number(apply->_uid), 80);
    row->setData(UidRole, apply->_uid);
    row->setData(IconRole, apply->_icon);
    row->setData(StatusRole, apply->_status);
    QString description = apply->_desc;
    description.replace(QLatin1Char('\n'), QLatin1Char(' '));
    description.replace(QLatin1Char('\r'), QLatin1Char(' '));
    description = description.trimmed();
    if (description.isEmpty())
      description = tr("暂无验证消息");
    row->setData(DescriptionRole, description);
    row->setToolTip(name + QLatin1Char('\n') + description);
    _applicationRows.append(row);
  }
  if (_applicationRows.isEmpty())
    _applicationRows.append(addRow(tr("暂无好友申请"), Empty, "no-applications", 44));
  SetApplicationsExpanded(_applicationsExpanded);
  for (const auto &title : {tr("群聊"), tr("公众号、服务号"), tr("企业微信联系人")})
    addRow(title, Category, title, 44)->setToolTip(tr("%1：暂未开放").arg(title));

  auto friends = UserMgr::GetInstance()->GetFriendList();
  friends.erase(std::remove_if(friends.begin(), friends.end(),
                               [](const auto &user) { return !user || user->_uid <= 0; }),
                friends.end());
  QCollator collator;
  collator.setCaseSensitivity(Qt::CaseInsensitive);
  collator.setNumericMode(true);
  std::stable_sort(friends.begin(), friends.end(), [&collator](const auto &a, const auto &b) {
    const QString aGroup = contactInitial(contactName(a));
    const QString bGroup = contactInitial(contactName(b));
    if (aGroup != bGroup) {
      if (aGroup == "#")
        return false;
      if (bGroup == "#")
        return true;
      return aGroup < bGroup;
    }
    const int comparison = collator.compare(contactName(a), contactName(b));
    return comparison == 0 ? a->_uid < b->_uid : comparison < 0;
  });
  _contactsHeader = addRow(tr("联系人"), ContactsHeader, "contacts", 44);
  _contactsHeader->setData(UidRole, static_cast<int>(friends.size()));
  _contactRows.append(addRow(tr("星标朋友"), Starred, "starred", 40));
  QString previousGroup;
  for (const auto &user : friends) {
    const QString group = contactInitial(contactName(user));
    if (previousGroup != group) {
      _contactRows.append(addRow(group, Alphabet, "group-" + group, 32));
      previousGroup = group;
    }
    auto *row = addRow(contactName(user), Friend, QString::number(user->_uid), 64);
    row->setData(UidRole, user->_uid);
    row->setData(IconRole, user->_icon);
    _contactRows.append(row);
  }
  if (friends.empty())
    _contactRows.append(addRow(tr("暂无联系人"), Empty, "empty", 48));
  SetContactsExpanded(_contactsExpanded);
  for (int i = 0; i < count(); ++i)
    if (item(i)->data(KeyRole).toString() == selectedKey && !item(i)->isHidden()) {
      setCurrentItem(item(i));
      break;
    }
  verticalScrollBar()->setValue(scroll);
}

void ContactUserList::SetContactsExpanded(bool expanded)
{
  _contactsExpanded = expanded;
  if (_contactsHeader)
    _contactsHeader->setData(ExpandedRole, expanded);
  for (auto *row : _contactRows)
    row->setHidden(!expanded);
}

void ContactUserList::SetApplicationsExpanded(bool expanded)
{
  _applicationsExpanded = expanded;
  if (_newFriends)
    _newFriends->setData(ExpandedRole, expanded);
  if (!expanded && _applicationRows.contains(currentItem()))
    setCurrentItem(nullptr);
  for (auto *row : _applicationRows)
    row->setHidden(!expanded);
}

void ContactUserList::ShowRedPoint(bool show)
{
  _redPoint = show;
  if (_newFriends)
    _newFriends->setData(UnreadRole, show);
}

void ContactUserList::SelectNewFriends()
{
  SetApplicationsExpanded(true);
  if (_newFriends)
    setCurrentItem(_newFriends);
}

bool ContactUserList::ActivateCurrent()
{
  auto *row = currentItem();
  if (!row || row->isHidden() || !(row->flags() & Qt::ItemIsSelectable))
    return false;
  slot_item_clicked(row);
  return true;
}

void ContactUserList::slot_item_clicked(QListWidgetItem *item)
{
  if (!item)
    return;
  switch (item->data(KindRole).toInt()) {
  case NewFriends:
    SetApplicationsExpanded(!_applicationsExpanded);
    if (_applicationsExpanded)
      emit sig_switch_apply_friend_page();
    break;
  case Application:
    for (const auto &apply : UserMgr::GetInstance()->GetApplyList())
      if (apply && apply->_uid == item->data(UidRole).toInt()) {
        emit sig_open_friend_apply(apply);
        break;
      }
    break;
  case ContactsHeader:
    SetContactsExpanded(!_contactsExpanded);
    break;
  case Friend:
    if (auto user = UserMgr::GetInstance()->GetFriendById(item->data(UidRole).toInt()))
      emit sig_switch_friend_info_page(user);
    break;
  case Starred:
  case Category:
    emit sig_show_placeholder(item->text(), tr("%1功能暂未开放").arg(item->text()));
    break;
  default:
    break;
  }
}
