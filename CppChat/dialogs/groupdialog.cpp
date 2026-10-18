#include "groupdialog.h"
#include "common/global.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include <QCollator>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>

namespace {
constexpr int SearchRole = Qt::UserRole + 1;
constexpr int NameRole = Qt::UserRole + 2;
constexpr int IconRole = Qt::UserRole + 3;

QString displayName(int uid, const QString &fallback)
{
  const auto user = UserMgr::GetInstance()->GetFriendById(uid);
  return user && !user->_back.trimmed().isEmpty() ? user->_back : fallback;
}

// Keep Qt's accessible/keyboard checkbox behavior and allow selection anywhere in a row.
class MemberDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                   const QModelIndex &index) override
  {
    if (index.flags().testFlag(Qt::ItemIsUserCheckable) &&
        index.flags().testFlag(Qt::ItemIsEnabled) && event->type() == QEvent::MouseButtonRelease &&
        static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
      return model->setData(index,
                            index.data(Qt::CheckStateRole).toInt() == Qt::Checked ? Qt::Unchecked
                                                                                  : Qt::Checked,
                            Qt::CheckStateRole);
    return QStyledItemDelegate::editorEvent(event, model, option, index);
  }

protected:
  void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
  {
    QStyledItemDelegate::initStyleOption(option, index);
    option->features |= QStyleOptionViewItem::HasDecoration;
    option->decorationSize = QSize(36, 36);
    option->icon = QIcon(UserMgr::GetInstance()->AvatarThumbnail(
        index.data(Qt::UserRole).toInt(), index.data(IconRole).toString(), QSize(36, 36),
        option->widget ? option->widget->devicePixelRatioF() : 1.0));
  }
};

QListWidget *memberList(QWidget *parent, const char *name)
{
  auto *list = new QListWidget(parent);
  list->setObjectName(name);
  list->setIconSize(QSize(36, 36));
  list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  list->setItemDelegate(new MemberDelegate(list));
  return list;
}

void fillMember(QListWidgetItem *item, int uid, const QString &name, const QString &account,
                const QString &icon, const QString &role = {})
{
  item->setText(role.isEmpty() ? name : name + QStringLiteral("  ·  ") + role);
  item->setToolTip(account + QStringLiteral("\nID: ") + QString::number(uid));
  item->setData(Qt::UserRole, uid);
  item->setData(SearchRole,
                name + QLatin1Char(' ') + account + QLatin1Char(' ') + QString::number(uid));
  item->setData(NameRole, name);
  item->setData(IconRole, icon);
  item->setSizeHint(QSize(0, 54));
}

bool confirm(QWidget *parent, const QString &title, const QString &text)
{
  QMessageBox box(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::No, parent);
  box.setTextFormat(Qt::PlainText);
  box.setDefaultButton(QMessageBox::No);
  return box.exec() == QMessageBox::Yes;
}
} // namespace

GroupDialog::GroupDialog(int threadId, QWidget *parent) : QDialog(parent), _threadId(threadId)
{
  BuildUi();
  BindActions();
  BindNetwork();
  Refresh({});
  if (const auto cached = UserMgr::GetInstance()->GetChatThreadByThreadId(_threadId))
    Refresh(cached->Group());
  if (_threadId)
    QTimer::singleShot(0, this, [this] { Send("info"); });
}

void GroupDialog::BuildUi()
{
  const int threadId = _threadId;
  setWindowTitle(threadId ? tr("群聊详情") : tr("发起群聊"));
  setObjectName("groupDialog");
  setProperty("groupDialog", true);
  resize(700, 650);
  setMinimumSize(580, 520);
  if (auto *display = screen()) {
    const auto available = display->availableGeometry().size() - QSize(48, 64);
    setMinimumSize(QSize(580, 520).boundedTo(available));
    resize(size().boundedTo(available));
  }
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(24, 20, 24, 20);
  outer->setSpacing(12);
  auto *scroll = new QScrollArea(this);
  scroll->setObjectName("groupScroll");
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *body = new QWidget(scroll);
  body->setBackgroundRole(QPalette::Base);
  body->setAutoFillBackground(true);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);
  scroll->setWidget(body);
  outer->addWidget(scroll, 1);
  _summary = new QLabel(this);
  _summary->setObjectName("groupSummary");
  _summary->setTextFormat(Qt::PlainText);
  _summary->setWordWrap(true);
  layout->addWidget(_summary);
  auto *nameRow = new QHBoxLayout;
  nameRow->addWidget(new QLabel(tr("群名称"), this));
  _name = new QLineEdit(this);
  _name->setObjectName("groupName");
  _name->setMaxLength(64);
  _name->setPlaceholderText(tr("可选，不填写时使用所选成员名称"));
  nameRow->addWidget(_name, 1);
  _save = new QPushButton(threadId ? tr("保存群名") : tr("创建"), this);
  _save->setObjectName("groupSave");
  _save->setProperty("primary", true);
  nameRow->addWidget(_save);
  layout->addLayout(nameRow);
  _search = new QLineEdit(this);
  _search->setObjectName("groupSearch");
  _search->setPlaceholderText(tr("搜索备注、用户名或 ID"));
  _search->setClearButtonEnabled(true);
  layout->addWidget(_search);
  _tabs = new QTabWidget(this);
  _tabs->setObjectName("groupTabs");
  _tabs->setMinimumHeight(250);
  auto *membersPage = new QWidget(_tabs);
  auto *membersLayout = new QVBoxLayout(membersPage);
  membersLayout->setContentsMargins(0, 12, 0, 0);
  _members = memberList(membersPage, "groupMembers");
  membersLayout->addWidget(_members);
  auto *memberActions = new QHBoxLayout;
  _remove = new QPushButton(tr("移出群聊"), membersPage);
  _remove->setObjectName("groupRemove");
  _transfer = new QPushButton(tr("转让群主"), membersPage);
  _transfer->setObjectName("groupTransfer");
  memberActions->addWidget(_remove);
  memberActions->addWidget(_transfer);
  memberActions->addStretch();
  membersLayout->addLayout(memberActions);
  _tabs->addTab(membersPage, tr("群成员"));
  auto *invitePage = new QWidget(_tabs);
  auto *inviteLayout = new QHBoxLayout(invitePage);
  inviteLayout->setContentsMargins(0, 12, 0, 0);
  auto *availableLayout = new QVBoxLayout;
  availableLayout->addWidget(new QLabel(tr("好友列表"), invitePage));
  _friends = memberList(invitePage, "groupFriends");
  availableLayout->addWidget(_friends);
  auto *selectedLayout = new QVBoxLayout;
  _selectionCount = new QLabel(invitePage);
  _selectionCount->setObjectName("groupSelectionCount");
  selectedLayout->addWidget(_selectionCount);
  _selected = memberList(invitePage, "groupSelected");
  _selected->setToolTip(tr("点击已选好友可取消选择"));
  selectedLayout->addWidget(_selected);
  _add = new QPushButton(tr("邀请所选好友"), invitePage);
  _add->setObjectName("groupInvite");
  _add->setProperty("primary", true);
  selectedLayout->addWidget(_add);
  inviteLayout->addLayout(availableLayout, 3);
  inviteLayout->addLayout(selectedLayout, 2);
  _tabs->addTab(invitePage, threadId ? tr("邀请好友") : tr("选择好友"));
  layout->addWidget(_tabs, 1);
  _empty = new QLabel(this);
  _empty->setObjectName("groupEmpty");
  _empty->setTextFormat(Qt::PlainText);
  _empty->setWordWrap(true);
  layout->addWidget(_empty);
  _status = new QLabel(this);
  _status->setObjectName("groupStatus");
  _status->setTextFormat(Qt::PlainText);
  _status->setWordWrap(true);
  _status->setMinimumHeight(22);
  layout->addWidget(_status);
  auto *footer = new QHBoxLayout;
  _leave = new QPushButton(tr("退出群聊"), this);
  _leave->setObjectName("groupLeave");
  _leave->setProperty("danger", true);
  footer->addWidget(_leave);
  footer->addStretch();
  _close = new QPushButton(tr("取消"), this);
  _close->setObjectName("groupClose");
  footer->addWidget(_close);
  outer->addLayout(footer);
  for (auto *button : findChildren<QPushButton *>()) {
    button->setCursor(Qt::PointingHandCursor);
    button->setAutoDefault(false);
  }
}

void GroupDialog::BindActions()
{
  connect(_close, &QPushButton::clicked, this, &QDialog::reject);
  connect(_search, &QLineEdit::textChanged, this, &GroupDialog::FilterLists);
  connect(_tabs, &QTabWidget::currentChanged, this, &GroupDialog::FilterLists);
  connect(_members, &QListWidget::currentItemChanged, this, &GroupDialog::UpdateActions);
  connect(_friends, &QListWidget::itemChanged, this, [this] {
    _creationKey.clear();
    UpdateSelection();
  });
  connect(_selected, &QListWidget::itemClicked, this, [this](QListWidgetItem *selected) {
    if (_busy)
      return;
    const int uid = selected->data(Qt::UserRole).toInt();
    for (int i = 0; i < _friends->count(); ++i)
      if (_friends->item(i)->data(Qt::UserRole).toInt() == uid) {
        _friends->item(i)->setCheckState(Qt::Unchecked);
        break;
      }
  });
  connect(_name, &QLineEdit::textChanged, this, [this] {
    _creationKey.clear();
    _status->clear();
    UpdateActions();
  });
  connect(_save, &QPushButton::clicked, this, [this] {
    const auto name = _threadId ? _name->text().trimmed() : CreationName();
    if (_threadId) {
      Send("rename", {{"name", name}});
      return;
    }
    QJsonArray members;
    for (int uid : CheckedFriends())
      members.append(uid);
    if (_creationKey.isEmpty())
      _creationKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
    Send("create", {{"name", name}, {"members", members}, {"creation_key", _creationKey}});
  });
  connect(_add, &QPushButton::clicked, this, [this] {
    _inviteQueue = CheckedFriends();
    _inviteCompleted = 0;
    InviteNext();
  });
  connect(_remove, &QPushButton::clicked, this, [this] {
    const auto *item = _members->currentItem();
    if (item &&
        confirm(
            this, tr("移出成员"),
            tr("将 %1 移出群聊？对方将无法继续收发群消息。").arg(item->data(NameRole).toString())))
      Send("remove", {{"member_uid", SelectedMember()}});
  });
  connect(_transfer, &QPushButton::clicked, this, [this] {
    const auto *item = _members->currentItem();
    if (item &&
        confirm(
            this, tr("转让群主"),
            tr("将群主转让给 %1？转让后你将成为普通成员。").arg(item->data(NameRole).toString())))
      Send("transfer", {{"member_uid", SelectedMember()}});
  });
  connect(_leave, &QPushButton::clicked, this, [this] {
    const bool owner = _owner == UserMgr::GetInstance()->GetUid();
    if (confirm(this, owner ? tr("解散群聊") : tr("退出群聊"),
                owner ? tr("解散后所有成员都无法继续收发消息。若只想退出，请先转让群主。")
                      : tr("退出后将无法继续读取和发送群消息，确定退出？")))
      Send(owner ? "disband" : "leave");
  });
}

void GroupDialog::BindNetwork()
{
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_group_result, this,
          [this](const QJsonObject &response) {
            if (!_busy || response["op"].toString() != _pendingOperation)
              return;
            if (_threadId && response["group"].toObject()["thread_id"].toInt() != _threadId)
              return;
            const auto operation = _pendingOperation;
            _pendingOperation.clear();
            _busy = false;
            const auto group = response["group"].toObject();
            if (group["removed"].toBool()) {
              accept();
              return;
            }
            const bool created = _threadId == 0;
            if (operation == "add" && !_inviteQueue.isEmpty()) {
              _inviteQueue.removeFirst();
              ++_inviteCompleted;
            }
            if (operation == "rename")
              _serverName.clear();
            Refresh(group);
            if (created) {
              emit groupOpened(_threadId);
              accept();
            } else if (!_inviteQueue.isEmpty()) {
              SetBusy(true);
              QTimer::singleShot(0, this, [this] {
                _busy = false;
                InviteNext();
              });
            } else
              _status->setText(operation == "info" ? QString()
                               : operation == "add"
                                   ? tr("已邀请 %1 位好友入群").arg(_inviteCompleted)
                                   : tr("群资料已更新"));
          });
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_request_failed, this,
          [this](ReqId id, const QString &message) {
            if (id == ID_GROUP_REQ && _busy) {
              _inviteQueue.clear();
              SetBusy(false);
              _status->setText(_inviteCompleted > 0
                                   ? tr("已邀请 %1 人，其余未完成：%2。可重新选择后重试。")
                                         .arg(_inviteCompleted)
                                         .arg(message)
                                   : message);
            }
          });
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_group_info, this,
          [this](const QJsonObject &group) {
            if (!_threadId || group["thread_id"].toInt() != _threadId || _busy)
              return;
            if (group["removed"].toBool()) {
              _owner = 0;
              _memberCount = 0;
              _status->setText(tr("你已不在这个群聊中，或该群已解散"));
              UpdateActions();
            } else if (group["members"].isArray())
              Refresh(group);
          });
  connect(UserMgr::GetInstance().get(), &UserMgr::sig_avatar_ready, this, [this] {
    for (auto *list : {_members, _friends, _selected})
      list->viewport()->update();
  });
  connect(this, &QDialog::finished, this, [this] {
    _inviteQueue.clear();
    if (_busy)
      TcpMgr::GetInstance()->CancelRequest(ID_GROUP_REQ);
  });
}

void GroupDialog::Send(const QString &operation, QJsonObject fields)
{
  if (_busy)
    return;
  if (TcpMgr::GetInstance()->HasPendingRequest(ID_GROUP_REQ)) {
    _status->setText(tr("另一个群聊请求正在处理中，请稍后重试"));
    return;
  }
  if (!TcpMgr::GetInstance()->SupportsGroupChat()) {
    _status->setText(tr("当前服务器尚未启用群聊，请先更新服务器"));
    return;
  }
  fields["op"] = operation;
  if (_threadId)
    fields["thread_id"] = _threadId;
  _pendingOperation = operation;
  if (operation != "add")
    _inviteCompleted = 0;
  SetBusy(true);
  _status->setText(operation == "add" ? tr("正在邀请好友，已完成 %1 人…").arg(_inviteCompleted)
                                      : tr("正在同步…"));
  emit TcpMgr::GetInstance()
      -> sig_send_data(ID_GROUP_REQ, QJsonDocument(fields).toJson(QJsonDocument::Compact));
}

void GroupDialog::SetBusy(bool busy)
{
  _busy = busy;
  UpdateActions();
}

void GroupDialog::UpdateActions()
{
  const bool editing = _threadId > 0;
  const bool owner = _owner > 0 && _owner == UserMgr::GetInstance()->GetUid();
  const auto selection = CheckedFriends();
  const auto name = _name->text().trimmed();
  const bool validName = !name.isEmpty() && name.toUtf8().size() <= 64;
  _save->setEnabled(!_busy && (editing ? owner && validName && name != _serverName
                                       : !selection.isEmpty() && selection.size() <= 31 &&
                                             CreationName().toUtf8().size() <= 64));
  _add->setEnabled(!_busy && owner && !selection.isEmpty() &&
                   selection.size() + _memberCount <= 32);
  const bool otherMember =
      SelectedMember() > 0 && SelectedMember() != UserMgr::GetInstance()->GetUid();
  _remove->setEnabled(!_busy && owner && otherMember);
  _transfer->setEnabled(!_busy && owner && otherMember);
  _leave->setEnabled(!_busy && _owner > 0);
  _friends->setEnabled(!_busy && (!editing || owner));
  _selected->setEnabled(!_busy && (!editing || owner));
  _name->setReadOnly(_busy || (editing && !owner));
  _search->setEnabled(!_busy);
  _close->setText(_busy ? tr("停止并关闭") : editing ? tr("完成") : tr("取消"));
  if (!_busy && !name.isEmpty() && name.toUtf8().size() > 64)
    _status->setText(tr("群名称过长，请缩短至 64 字节以内（中文约 21 字）"));
}

int GroupDialog::SelectedMember() const
{
  const auto *item = _members->currentItem();
  return item && !item->isHidden() ? item->data(Qt::UserRole).toInt() : 0;
}

QList<int> GroupDialog::CheckedFriends() const
{
  QList<int> ids;
  for (int i = 0; i < _friends->count(); ++i)
    if (_friends->item(i)->checkState() == Qt::Checked)
      ids.append(_friends->item(i)->data(Qt::UserRole).toInt());
  return ids;
}

QString GroupDialog::CreationName() const
{
  if (!_name->text().trimmed().isEmpty())
    return _name->text().trimmed();
  QStringList names;
  for (int i = 0; i < _friends->count() && names.size() < 3; ++i)
    if (_friends->item(i)->checkState() == Qt::Checked)
      names.append(_friends->item(i)->data(NameRole).toString());
  const auto points = names.join(QStringLiteral("、")).toUcs4();
  QString name;
  for (const auto point : points) {
    const auto scalar = static_cast<char32_t>(point);
    const auto next = name + QString::fromUcs4(&scalar, 1);
    if (next.toUtf8().size() > 54)
      break;
    name = next;
  }
  return name.isEmpty() ? tr("新群聊") : name + tr("的群聊");
}

void GroupDialog::UpdateSelection()
{
  _selected->clear();
  for (int i = 0; i < _friends->count(); ++i) {
    const auto *friendItem = _friends->item(i);
    if (friendItem->checkState() != Qt::Checked)
      continue;
    auto *item = new QListWidgetItem(*friendItem);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setData(Qt::CheckStateRole, QVariant());
    item->setToolTip(tr("点击取消选择"));
    _selected->addItem(item);
  }
  const int selected = _selected->count();
  const int availableSlots = _threadId ? qMax(0, 32 - _memberCount) : 31;
  _selectionCount->setText(tr("已选 %1 / %2 人").arg(selected).arg(availableSlots));
  _selectionCount->setToolTip(tr("包含自己，群聊最多 32 人；搜索不会清除已选好友"));
  if (!_busy)
    _status->setText(selected > availableSlots
                         ? tr("超过群人数上限，请取消 %1 位好友").arg(selected - availableSlots)
                         : QString());
  UpdateActions();
}

void GroupDialog::FilterLists()
{
  const auto keyword = _search->text().trimmed();
  for (auto *list : {_members, _friends})
    for (int i = 0; i < list->count(); ++i)
      list->item(i)->setHidden(
          !list->item(i)->data(SearchRole).toString().contains(keyword, Qt::CaseInsensitive));
  auto *list = _tabs->currentIndex() == 0 ? _members : _friends;
  int visible = 0;
  for (int i = 0; i < list->count(); ++i)
    visible += !list->item(i)->isHidden();
  _empty->setVisible(visible == 0);
  _empty->setText(!keyword.isEmpty() ? tr("没有匹配的人，请试试备注、用户名或 ID")
                  : list == _friends ? (_threadId ? tr("没有可邀请的好友，好友可能都已在群内")
                                                  : tr("还没有好友，请先添加好友再发起群聊"))
                                     : tr("群成员尚未加载"));
  UpdateActions();
}

void GroupDialog::InviteNext()
{
  if (!_inviteQueue.isEmpty())
    Send("add", {{"member_uid", _inviteQueue.first()}});
}

void GroupDialog::Refresh(const QJsonObject &group)
{
  const auto selectedIds = CheckedFriends();
  const int selectedMember = SelectedMember();
  const bool dirtyName = !_serverName.isEmpty() && _name->text().trimmed() != _serverName;
  if (!group.isEmpty()) {
    _threadId = group["thread_id"].toInt();
    _owner = group["owner_uid"].toInt();
    _serverName = group["name"].toString();
    if (!dirtyName)
      _name->setText(_serverName);
  }
  const QSignalBlocker blocker(_friends);
  _members->clear();
  QSet<int> existing;
  for (const auto &value : group["members"].toArray()) {
    const auto member = value.toObject();
    const int uid = member["uid"].toInt();
    existing.insert(uid);
    const auto friendInfo = UserMgr::GetInstance()->GetFriendById(uid);
    auto *item = new QListWidgetItem(_members);
    QString role = uid == _owner ? tr("群主") : QString();
    if (uid == UserMgr::GetInstance()->GetUid())
      role = role.isEmpty() ? tr("我") : role + tr(" · 我");
    fillMember(item, uid, displayName(uid, member["name"].toString()), member["name"].toString(),
               friendInfo ? friendInfo->_icon : QString(), role);
    if (uid == selectedMember)
      _members->setCurrentItem(item);
  }
  _memberCount = existing.size();
  _friends->clear();
  auto friends = UserMgr::GetInstance()->GetFriendList();
  QCollator collator(QLocale(QLocale::Chinese, QLocale::China));
  collator.setCaseSensitivity(Qt::CaseInsensitive);
  std::stable_sort(friends.begin(), friends.end(), [&collator](const auto &a, const auto &b) {
    return collator.compare(displayName(a->_uid, a->_name), displayName(b->_uid, b->_name)) < 0;
  });
  for (const auto &user : friends) {
    if (existing.contains(user->_uid))
      continue;
    auto *item = new QListWidgetItem(_friends);
    fillMember(item, user->_uid, displayName(user->_uid, user->_name), user->_name, user->_icon);
    item->setCheckState(selectedIds.contains(user->_uid) ? Qt::Checked : Qt::Unchecked);
  }
  const bool editing = _threadId > 0;
  const bool owner = _owner == UserMgr::GetInstance()->GetUid();
  _tabs->setTabVisible(0, editing);
  _tabs->setTabText(0, tr("群成员（%1）").arg(_memberCount));
  _tabs->setTabVisible(1, !editing || owner);
  _tabs->setTabText(1, editing ? tr("邀请好友") : tr("选择好友"));
  _summary->setText(editing ? tr("%1 位成员 · 群聊 ID %2 · %3")
                                  .arg(_memberCount)
                                  .arg(_threadId)
                                  .arg(owner ? tr("你是群主") : tr("群名称和成员由群主管理"))
                            : tr("选择好友，开始一段群聊"));
  for (auto *button : {_remove, _transfer})
    button->setVisible(editing && owner);
  _add->setVisible(editing && owner);
  _leave->setVisible(editing);
  _leave->setText(owner ? tr("解散群聊") : tr("退出群聊"));
  _save->setText(editing ? tr("保存群名") : tr("创建"));
  _save->setVisible(!editing || owner);
  UpdateSelection();
  FilterLists();
}
