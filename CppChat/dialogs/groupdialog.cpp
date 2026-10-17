#include "groupdialog.h"
#include "common/global.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

GroupDialog::GroupDialog(int threadId, QWidget *parent) : QDialog(parent), _threadId(threadId)
{
  setWindowTitle(threadId ? tr("群聊管理") : tr("创建群聊"));
  setObjectName("groupDialog");
  resize(580, 620);
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(tr("群名称（最多 64 字节，群聊最多 32 人）"), this));
  _name = new QLineEdit(this);
  _name->setObjectName("groupName");
  _name->setMaxLength(64);
  layout->addWidget(_name);
  layout->addWidget(new QLabel(tr("当前成员"), this));
  _members = new QListWidget(this);
  _members->setObjectName("groupMembers");
  layout->addWidget(_members);
  layout->addWidget(
      new QLabel(threadId ? tr("选择好友邀请入群") : tr("勾选好友（至少一位）"), this));
  _friends = new QListWidget(this);
  _friends->setObjectName("groupFriends");
  layout->addWidget(_friends);
  _status = new QLabel(this);
  _status->setWordWrap(true);
  layout->addWidget(_status);
  auto *actions = new QHBoxLayout;
  layout->addLayout(actions);
  _save = new QPushButton(threadId ? tr("保存群名") : tr("创建"), this);
  _add = new QPushButton(tr("邀请"), this);
  _remove = new QPushButton(tr("移出"), this);
  _transfer = new QPushButton(tr("转让群主"), this);
  _leave = new QPushButton(tr("退出群聊"), this);
  for (auto *button : {_save, _add, _remove, _transfer, _leave})
    actions->addWidget(button);
  connect(_save, &QPushButton::clicked, this, [this] {
    const auto name = _name->text().trimmed();
    if (name.isEmpty() || name.toUtf8().size() > 64) {
      _status->setText(tr("请输入群名，中文最多约 21 字"));
      return;
    }
    if (_threadId) {
      Send("rename", {{"name", name}});
      return;
    }
    QJsonArray members;
    for (int i = 0; i < _friends->count(); ++i)
      if (_friends->item(i)->checkState() == Qt::Checked)
        members.append(_friends->item(i)->data(Qt::UserRole).toInt());
    if (members.isEmpty() || members.size() > 31) {
      _status->setText(tr("请选择 1 至 31 位好友"));
      return;
    }
    if (_creationKey.isEmpty())
      _creationKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
    Send("create", {{"name", name}, {"members", members}, {"creation_key", _creationKey}});
  });
  connect(_name, &QLineEdit::textEdited, this, [this] { _creationKey.clear(); });
  connect(_friends, &QListWidget::itemChanged, this, [this] { _creationKey.clear(); });
  connect(_add, &QPushButton::clicked, this, [this] {
    if (const auto *item = _friends->currentItem())
      Send("add", {{"member_uid", item->data(Qt::UserRole).toInt()}});
  });
  connect(_remove, &QPushButton::clicked, this, [this] {
    if (SelectedMember() > 0 &&
        QMessageBox::question(this, tr("移出成员"), tr("确定将选中成员移出群聊？")) ==
            QMessageBox::Yes)
      Send("remove", {{"member_uid", SelectedMember()}});
  });
  connect(_transfer, &QPushButton::clicked, this, [this] {
    if (SelectedMember() > 0 &&
        QMessageBox::question(this, tr("转让群主"), tr("转让后你将成为普通成员，确定继续？")) ==
            QMessageBox::Yes)
      Send("transfer", {{"member_uid", SelectedMember()}});
  });
  connect(_leave, &QPushButton::clicked, this, [this] {
    const bool owner = _owner == UserMgr::GetInstance()->GetUid();
    if (QMessageBox::question(this, owner ? tr("解散群聊") : tr("退出群聊"),
                              owner ? tr("确定解散？所有成员将无法继续收发消息。")
                                    : tr("确定退出？退出后将无法继续读取群消息。")) ==
        QMessageBox::Yes)
      Send(owner ? "disband" : "leave");
  });
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_group_result, this,
          [this](const QJsonObject &response) {
            if (!_busy)
              return;
            SetBusy(false);
            const auto group = response["group"].toObject();
            if (group["removed"].toBool()) {
              accept();
              return;
            }
            const bool created = _threadId == 0;
            Refresh(group);
            _status->setText(tr("已同步"));
            if (created) {
              emit groupOpened(_threadId);
              accept();
            }
          });
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_request_failed, this,
          [this](ReqId id, const QString &message) {
            if (id == ID_GROUP_REQ && _busy) {
              SetBusy(false);
              _status->setText(message);
            }
          });
  connect(this, &QDialog::finished, this, [this] {
    if (_busy)
      TcpMgr::GetInstance()->CancelRequest(ID_GROUP_REQ);
  });
  Refresh({});
  if (_threadId)
    QTimer::singleShot(0, this, [this] { Send("info"); });
}

void GroupDialog::Send(const QString &operation, QJsonObject fields)
{
  if (_busy)
    return;
  if (!TcpMgr::GetInstance()->SupportsGroupChat()) {
    _status->setText(tr("当前服务器尚未启用群聊，请先更新服务器"));
    return;
  }
  fields["op"] = operation;
  if (_threadId)
    fields["thread_id"] = _threadId;
  SetBusy(true);
  _status->setText(tr("正在同步…"));
  emit TcpMgr::GetInstance()
      -> sig_send_data(ID_GROUP_REQ, QJsonDocument(fields).toJson(QJsonDocument::Compact));
}

void GroupDialog::SetBusy(bool busy)
{
  _busy = busy;
  const bool owner = _owner == UserMgr::GetInstance()->GetUid();
  _save->setEnabled(!busy && (!_threadId || owner));
  for (auto *button : {_add, _remove, _transfer})
    button->setEnabled(!busy && owner);
  _leave->setEnabled(!busy && _owner > 0);
  _friends->setEnabled(!busy && (!_threadId || owner));
  _name->setReadOnly(busy || (_threadId && !owner));
}

int GroupDialog::SelectedMember() const
{
  return _members->currentItem() ? _members->currentItem()->data(Qt::UserRole).toInt() : 0;
}

void GroupDialog::Refresh(const QJsonObject &group)
{
  if (!group.isEmpty()) {
    _threadId = group["thread_id"].toInt();
    _owner = group["owner_uid"].toInt();
    _name->setText(group["name"].toString());
  }
  _members->clear();
  QSet<int> existing;
  for (const auto &value : group["members"].toArray()) {
    const auto member = value.toObject();
    const int uid = member["uid"].toInt();
    existing.insert(uid);
    const auto friendInfo = UserMgr::GetInstance()->GetFriendById(uid);
    QString name =
        friendInfo && !friendInfo->_back.isEmpty() ? friendInfo->_back : member["name"].toString();
    auto *item = new QListWidgetItem(
        QString("%1 (%2)%3").arg(name).arg(uid).arg(uid == _owner ? tr(" · 群主") : QString()),
        _members);
    item->setData(Qt::UserRole, uid);
  }
  _friends->clear();
  for (const auto &user : UserMgr::GetInstance()->GetFriendList()) {
    if (existing.contains(user->_uid))
      continue;
    auto *item = new QListWidgetItem(user->_back.isEmpty() ? user->_name : user->_back, _friends);
    item->setData(Qt::UserRole, user->_uid);
    if (!_threadId)
      item->setCheckState(Qt::Unchecked);
  }
  const bool editing = _threadId > 0;
  _members->setVisible(editing);
  for (auto *button : {_add, _remove, _transfer, _leave})
    button->setVisible(editing);
  _leave->setText(_owner == UserMgr::GetInstance()->GetUid() ? tr("解散群聊") : tr("退出群聊"));
  SetBusy(false);
}
