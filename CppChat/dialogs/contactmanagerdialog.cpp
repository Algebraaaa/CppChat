#include "contactmanagerdialog.h"
#include "applyfriend.h"
#include "common/chatstore.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

namespace {
constexpr int TagsRole = Qt::UserRole + 1, StarRole = Qt::UserRole + 2;
class ContactFilter : public QSortFilterProxyModel {
public:
  QString tag;
  bool starred = false;
  using QSortFilterProxyModel::QSortFilterProxyModel;
  void RefreshFilter() { invalidate(); }

protected:
  bool filterAcceptsRow(int row, const QModelIndex &parent) const override
  {
    const auto index = sourceModel()->index(row, 0, parent);
    return (!starred || index.data(StarRole).toBool()) &&
           (tag.isEmpty() || index.data(TagsRole).toStringList().contains(tag)) &&
           QSortFilterProxyModel::filterAcceptsRow(row, parent);
  }
};
} // namespace

ContactManagerDialog::ContactManagerDialog(QWidget *parent)
    : QDialog(parent), _uid(UserMgr::GetInstance()->GetUid())
{
  setObjectName("contactManagerDialog");
  setProperty("groupDialog", true);
  setWindowTitle(tr("通讯录管理 · 好友"));
  resize(QSize(920, 590).boundedTo(screen()->availableGeometry().size() - QSize(40, 60)));
  auto *layout = new QVBoxLayout(this);
  auto *filters = new QHBoxLayout;
  _search = new QLineEdit(this);
  _search->setObjectName("contactManagerSearch");
  _search->setPlaceholderText(tr("搜索账号、昵称、备注、标签或 UID"));
  _search->setMaxLength(256);
  _tag = new QComboBox(this);
  _tag->setObjectName("contactManagerTag");
  _starred = new QCheckBox(tr("仅星标朋友"), this);
  _starred->setObjectName("contactManagerStarred");
  filters->addWidget(_search, 1);
  filters->addWidget(_tag);
  filters->addWidget(_starred);
  layout->addLayout(filters);
  _model = new QStandardItemModel(this);
  auto *proxy = new ContactFilter(this);
  _proxy = proxy;
  proxy->setSourceModel(_model);
  proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
  proxy->setFilterKeyColumn(-1);
  proxy->setSortLocaleAware(true);
  _view = new QTableView(this);
  _view->setObjectName("contactManagerTable");
  _view->setModel(proxy);
  _view->setSelectionBehavior(QAbstractItemView::SelectRows);
  _view->setSelectionMode(QAbstractItemView::SingleSelection);
  _view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  _view->setSortingEnabled(true);
  _view->verticalHeader()->hide();
  _view->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  _view->setWordWrap(false);
  layout->addWidget(_view, 1);
  _status = new QLabel(this);
  _status->setObjectName("contactManagerStatus");
  _status->setTextFormat(Qt::PlainText);
  layout->addWidget(_status);
  auto *buttons = new QDialogButtonBox(this);
  _profile = buttons->addButton(tr("查看资料"), QDialogButtonBox::ActionRole);
  _profile->setObjectName("contactManagerProfile");
  _edit = buttons->addButton(tr("编辑备注 / 标签"), QDialogButtonBox::ActionRole);
  _edit->setObjectName("contactManagerEdit");
  _star = buttons->addButton(tr("设为星标"), QDialogButtonBox::ActionRole);
  _star->setObjectName("contactManagerStar");
  auto *refresh = buttons->addButton(tr("刷新"), QDialogButtonBox::ActionRole);
  buttons->addButton(tr("关闭"), QDialogButtonBox::RejectRole);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(refresh, &QPushButton::clicked, this, &ContactManagerDialog::Refresh);
  connect(_search, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
  connect(_tag, &QComboBox::currentIndexChanged, this, [this, proxy] {
    proxy->tag = _tag->currentData().toString();
    proxy->RefreshFilter();
    Controls();
  });
  connect(_starred, &QCheckBox::toggled, this, [this, proxy](bool checked) {
    proxy->starred = checked;
    proxy->RefreshFilter();
    Controls();
  });
  connect(_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
          &ContactManagerDialog::Controls);
  connect(proxy, &QAbstractItemModel::rowsInserted, this, &ContactManagerDialog::Controls);
  connect(proxy, &QAbstractItemModel::rowsRemoved, this, &ContactManagerDialog::Controls);
  connect(_profile, &QPushButton::clicked, this, [this] {
    const int uid = SelectedUid();
    if (uid) {
      emit openProfileRequested(uid);
      accept();
    }
  });
  connect(_view, &QTableView::doubleClicked, this, [this] { _profile->click(); });
  connect(_star, &QPushButton::clicked, this, [this] {
    const int uid = SelectedUid();
    if (!uid)
      return;
    const bool saved = UserMgr::GetInstance()->SetStarredFriend(
        uid, !UserMgr::GetInstance()->IsStarredFriend(uid));
    Controls();
    if (!saved)
      _status->setText(tr("星标保存失败，请检查本机目录权限"));
  });
  connect(_edit, &QPushButton::clicked, this, [this] {
    const auto user = UserMgr::GetInstance()->GetFriendById(SelectedUid());
    if (!user || !TcpMgr::GetInstance()->IsConnected())
      return;
    if (_metadata) {
      _metadata->raise();
      return;
    }
    auto *dialog = new ApplyFriend(this);
    _metadata = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->SetContactInfo(user);
    dialog->open();
  });
  connect(UserMgr::GetInstance().get(), &UserMgr::sig_starred_changed, this,
          [this](int, bool) { Refresh(); });
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_contact_tags_updated, this,
          [this](int) { Refresh(); });
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_friend_deleted, this,
          [this](int) { Refresh(); });
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_connection_closed, this,
          &ContactManagerDialog::Controls);
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_swich_chatdlg, this,
          &ContactManagerDialog::Controls);
  connect(ChatStore::instance(), &ChatStore::accountChanged, this, [this] {
    if (_metadata)
      _metadata->QDialog::reject();
    _model->clear();
    reject();
  });
  Refresh();
}

int ContactManagerDialog::SelectedUid() const
{
  if (UserMgr::GetInstance()->GetUid() != _uid)
    return 0;
  const auto selection = _view->selectionModel()->selectedRows();
  if (selection.size() != 1)
    return 0;
  const int uid = _proxy->mapToSource(selection.first()).data(Qt::UserRole).toInt();
  return UserMgr::GetInstance()->CheckFriendById(uid) ? uid : 0;
}
void ContactManagerDialog::Controls()
{
  const int uid = SelectedUid();
  _profile->setEnabled(uid > 0);
  _star->setEnabled(uid > 0);
  _star->setText(UserMgr::GetInstance()->IsStarredFriend(uid) ? tr("取消星标") : tr("设为星标"));
  _edit->setEnabled(uid > 0 && TcpMgr::GetInstance()->IsConnected());
  _edit->setToolTip(TcpMgr::GetInstance()->IsConnected() ? tr("复用现有备注和标签保存流程")
                                                         : tr("连接服务器后可编辑备注和标签"));
  _status->setText(tr("当前显示 %1 / %2 位好友；双击查看资料，删除好友仍在资料的更多菜单中操作")
                       .arg(_proxy->rowCount())
                       .arg(_model->rowCount()));
}
void ContactManagerDialog::Refresh()
{
  if (UserMgr::GetInstance()->GetUid() != _uid) {
    _model->clear();
    reject();
    return;
  }
  const int selected = SelectedUid();
  const auto tag = _tag->currentData().toString();
  _model->clear();
  _model->setHorizontalHeaderLabels(
      {tr("账号"), tr("昵称"), tr("备注"), tr("标签"), tr("星标"), tr("UID")});
  QSet<QString> tags;
  for (const auto &user : UserMgr::GetInstance()->GetFriendList()) {
    if (!user || !UserMgr::GetInstance()->CheckFriendById(user->_uid))
      continue;
    QList<QStandardItem *> row;
    for (const auto &text :
         {user->_name, user->_nick, user->_back, user->_tags.join("、"),
          UserMgr::GetInstance()->IsStarredFriend(user->_uid) ? tr("是") : tr("否"),
          QString::number(user->_uid)})
      row.append(new QStandardItem(text));
    row.first()->setData(user->_uid, Qt::UserRole);
    row.first()->setData(user->_tags, TagsRole);
    row.first()->setData(UserMgr::GetInstance()->IsStarredFriend(user->_uid), StarRole);
    _model->appendRow(row);
    for (const auto &value : user->_tags)
      tags.insert(value);
  }
  {
    const QSignalBlocker blocked(_tag);
    _tag->clear();
    _tag->addItem(tr("全部标签"), QString());
    auto sorted = tags.values();
    sorted.sort(Qt::CaseInsensitive);
    for (const auto &value : sorted)
      _tag->addItem(value, value);
    _tag->setCurrentIndex(qMax(0, _tag->findData(tag)));
  }
  auto *proxy = static_cast<ContactFilter *>(_proxy);
  proxy->tag = _tag->currentData().toString();
  proxy->RefreshFilter();
  for (int row = 0; row < _proxy->rowCount(); ++row)
    if (_proxy->index(row, 0).data(Qt::UserRole).toInt() == selected) {
      _view->selectRow(row);
      break;
    }
  Controls();
}
