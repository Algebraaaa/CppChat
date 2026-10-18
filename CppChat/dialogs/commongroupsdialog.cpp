#include "commongroupsdialog.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

CommonGroupsDialog::CommonGroupsDialog(int friendUid, QWidget *parent)
    : QDialog(parent), _friendUid(friendUid)
{
  setObjectName("commonGroupsDialog");
  setProperty("groupDialog", true);
  setWindowTitle(tr("共同群聊"));
  resize(460, 480);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 24, 24, 20);
  layout->setSpacing(12);
  _status = new QLabel(this);
  _status->setObjectName("commonGroupsStatus");
  _status->setTextFormat(Qt::PlainText);
  _status->setWordWrap(true);
  layout->addWidget(_status);
  _list = new QListWidget(this);
  _list->setObjectName("commonGroupsList");
  _list->setIconSize(QSize(32, 32));
  _list->setUniformItemSizes(true);
  layout->addWidget(_list, 1);
  auto *buttons = new QDialogButtonBox(this);
  _refresh = buttons->addButton(tr("刷新"), QDialogButtonBox::ActionRole);
  _more = buttons->addButton(tr("加载更多"), QDialogButtonBox::ActionRole);
  _more->setObjectName("loadMoreCommonGroups");
  _open = buttons->addButton(tr("打开群聊"), QDialogButtonBox::ActionRole);
  _open->setObjectName("openCommonGroup");
  _open->setProperty("primary", true);
  buttons->addButton(tr("关闭"), QDialogButtonBox::RejectRole);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(_refresh, &QPushButton::clicked, this, [this] { Load(true); });
  connect(_more, &QPushButton::clicked, this, [this] { Load(false); });
  connect(_open, &QPushButton::clicked, this, &CommonGroupsDialog::OpenSelected);
  connect(_list, &QListWidget::itemActivated, this, [this] { OpenSelected(); });
  connect(_list, &QListWidget::itemSelectionChanged, this, &CommonGroupsDialog::UpdateControls);
  auto tcp = TcpMgr::GetInstance();
  connect(tcp.get(), &TcpMgr::sig_group_result, this, &CommonGroupsDialog::Receive);
  connect(tcp.get(), &TcpMgr::sig_request_failed, this, [this](ReqId id, const QString &error) {
    if (id != ID_GROUP_REQ || !_busy)
      return;
    _busy = false;
    _status->setText(error);
    UpdateControls();
  });
  connect(tcp.get(), &TcpMgr::sig_connection_closed, this, [this] {
    _busy = false;
    _status->setText(tr("聊天连接已断开，请重新登录后重试"));
    UpdateControls();
  });
  connect(tcp.get(), &TcpMgr::sig_friend_deleted, this, [this](int uid) {
    if (uid == _friendUid)
      reject();
  });
  connect(this, &QDialog::finished, this, [this] {
    if (_busy)
      TcpMgr::GetInstance()->CancelRequest(ID_GROUP_REQ);
    _busy = false;
  });
  UpdateControls();
  QTimer::singleShot(0, this, [this] { Load(true); });
}

void CommonGroupsDialog::Load(bool reset)
{
  auto tcp = TcpMgr::GetInstance();
  if (_busy || !isVisible())
    return;
  if (!tcp->IsConnected() || !tcp->SupportsCommonGroups()) {
    _status->setText(tr("当前服务器未支持共同群聊，或连接已断开，请更新服务器后重试"));
    return;
  }
  if (tcp->HasPendingRequest(ID_GROUP_REQ)) {
    _status->setText(tr("另一个群聊请求正在处理中，请稍后重试"));
    return;
  }
  if (reset) {
    _groups.clear();
    _list->clear();
    _cursor = 0;
    _hasMore = true;
  }
  _busy = true;
  _status->setText(tr("正在查询共同群聊…"));
  UpdateControls();
  const QJsonObject request{{"op", "common"}, {"friend_uid", _friendUid}, {"after", _cursor}};
  emit tcp->sig_send_data(ID_GROUP_REQ, QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void CommonGroupsDialog::Receive(const QJsonObject &response)
{
  if (!_busy || response["op"] != "common")
    return;
  const auto result = response["group"].toObject();
  _busy = false;
  const auto values = result["groups"].toArray();
  bool valid = result["friend_uid"].toInt() == _friendUid && result["groups"].isArray() &&
               result["has_more"].isBool() && result["next_thread_id"].isDouble() &&
               values.size() <= 50;
  int previous = _cursor;
  for (const auto &value : values) {
    const auto group = value.toObject();
    const int id = group["thread_id"].toInt();
    valid = valid && id > previous && group["name"].isString() && group["owner_uid"].toInt() > 0;
    previous = id;
  }
  valid = valid && result["next_thread_id"].toInt() == previous &&
          (!result["has_more"].toBool() || !values.isEmpty());
  if (!valid) {
    _status->setText(tr("共同群聊响应格式错误，请重试"));
    UpdateControls();
    return;
  }
  for (const auto &value : values) {
    const auto group = value.toObject();
    const int id = group["thread_id"].toInt();
    _groups.insert(id, group);
    auto *item = new QListWidgetItem(QIcon(":/icons/action-group-chat.svg"),
                                     group["name"].toString(), _list);
    item->setData(Qt::UserRole, id);
    item->setSizeHint(QSize(0, 56));
    item->setToolTip(item->text());
  }
  _cursor = previous;
  _hasMore = result["has_more"].toBool();
  _status->setText(_groups.isEmpty() ? tr("暂无共同群聊")
                                     : tr("已加载 %1 个共同群聊%2")
                                           .arg(_groups.size())
                                           .arg(_hasMore ? tr("，可继续加载") : QString()));
  UpdateControls();
}

void CommonGroupsDialog::UpdateControls()
{
  _refresh->setEnabled(!_busy);
  _more->setEnabled(!_busy && _hasMore);
  _open->setEnabled(!_busy && _list->currentItem());
}

void CommonGroupsDialog::OpenSelected()
{
  if (_busy || !_list->currentItem() || !UserMgr::GetInstance()->CheckFriendById(_friendUid))
    return;
  const int id = _list->currentItem()->data(Qt::UserRole).toInt();
  emit TcpMgr::GetInstance() -> sig_group_info(_groups.value(id));
  emit groupOpened(id);
  accept();
}
