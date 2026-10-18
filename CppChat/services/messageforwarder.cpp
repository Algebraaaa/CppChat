#include "messageforwarder.h"
#include "chatsyncservice.h"
#include "common/chatstore.h"
#include "dialogs/chatpage.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

MessageForwarder::MessageForwarder(QWidget *owner, ChatPage *page, ChatSyncService *sync)
    : QObject(owner), _owner(owner), _page(page), _sync(sync)
{
  auto tcp = TcpMgr::GetInstance();
  connect(ChatStore::instance(), &ChatStore::accountChanged, this, &MessageForwarder::Reset);
  connect(tcp.get(), &TcpMgr::sig_connection_closed, this, &MessageForwarder::Reset);
  connect(tcp.get(), &TcpMgr::sig_create_private_chat, this,
          [this](int uid, int other, int thread) {
            if (!_dialog || !_pendingUid || uid != _uid || other != _pendingUid ||
                ChatStore::instance()->Generation() != _generation)
              return;
            _pendingUid = 0;
            const auto data = _sync->EnsureThread(thread, other);
            if (!data || data->GetOtherId() != other) {
              _status->setText(tr("服务器返回的会话与所选目标不一致，请重试"));
              _list->setEnabled(true);
              _send->setEnabled(true);
              return;
            }
            _sync->QueueHistory(thread);
            Send(thread, other);
          });
  connect(tcp.get(), &TcpMgr::sig_request_failed, this, [this](ReqId id, const QString &error) {
    if (id == ID_CREATE_PRIVATE_CHAT_REQ && _dialog && _pendingUid) {
      _pendingUid = 0;
      _status->setText(error);
      _list->setEnabled(true);
      _send->setEnabled(true);
    }
  });
}

void MessageForwarder::Reset()
{
  const int pending = _pendingUid;
  _pendingUid = 0;
  auto dialog = _dialog;
  _dialog.clear();
  _messages.clear();
  _list = nullptr;
  _status = nullptr;
  _send = nullptr;
  if (pending)
    TcpMgr::GetInstance()->CancelRequest(ID_CREATE_PRIVATE_CHAT_REQ);
  if (dialog)
    dialog->close();
}

void MessageForwarder::Begin(QVector<StoredFavorite> snapshots)
{
  if (_dialog) {
    _dialog->raise();
    _dialog->activateWindow();
    return;
  }
  if (!ChatStore::instance()->IsReady() || snapshots.isEmpty() || snapshots.size() > 50 ||
      !TcpMgr::GetInstance()->IsConnected()) {
    emit status(tr("请连接服务器后重试，原消息和收藏仍然保留"));
    return;
  }
  qsizetype bytes = 0;
  bool images = false;
  for (const auto &value : snapshots) {
    images |= value.type == 1;
    bytes += value.media.size();
    if ((value.type != 0 && value.type != 1) ||
        (value.type == 1 && (value.media.isEmpty() || value.media.size() > 20 * 1024 * 1024))) {
      emit status(tr("媒体未能完整读取，无法转发"));
      return;
    }
  }
  if (bytes > 64 * 1024 * 1024) {
    emit status(tr("所选媒体超过 64 MiB，请分批转发"));
    return;
  }
  _messages = std::move(snapshots);
  _generation = ChatStore::instance()->Generation();
  auto mgr = UserMgr::GetInstance();
  _uid = mgr->GetUid();
  auto *dialog = new QDialog(_owner);
  _dialog = dialog;
  dialog->setObjectName("forwardDialog");
  dialog->setProperty("groupDialog", true);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowModality(Qt::WindowModal);
  dialog->setWindowTitle(tr("发送给…"));
  dialog->resize(480, 540);
  auto *layout = new QVBoxLayout(dialog);
  auto *search = new QLineEdit(dialog);
  search->setObjectName("forwardSearch");
  search->setPlaceholderText(tr("搜索好友或群聊"));
  layout->addWidget(search);
  _list = new QListWidget(dialog);
  _list->setObjectName("forwardTargets");
  for (const auto &user : mgr->GetFriendList()) {
    if (!user || user->_uid == _uid)
      continue;
    auto *item = new QListWidgetItem(user->_back.isEmpty() ? user->_name : user->_back, _list);
    item->setData(Qt::UserRole, user->_uid);
    item->setData(Qt::UserRole + 1, 0);
  }
  if (!images)
    for (const auto &data : mgr->GetChatThreads()) {
      if (!data || !data->IsGroup())
        continue;
      auto *item = new QListWidgetItem(tr("[群聊] %1").arg(data->GetGroupName()), _list);
      item->setData(Qt::UserRole, 0);
      item->setData(Qt::UserRole + 1, data->GetThreadId());
    }
  layout->addWidget(_list, 1);
  _status = new QLabel(tr("将 %1 条消息逐条发送，保留目标会话已有草稿。%2")
                           .arg(_messages.size())
                           .arg(images ? tr("图片当前仅支持发送给好友。") : QString()),
                       dialog);
  _status->setTextFormat(Qt::PlainText);
  _status->setWordWrap(true);
  layout->addWidget(_status);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, dialog);
  _send = buttons->addButton(tr("发送"), QDialogButtonBox::ActionRole);
  _send->setObjectName("forwardSend");
  _send->setEnabled(false);
  layout->addWidget(buttons);
  connect(_send, &QPushButton::clicked, this, &MessageForwarder::Choose);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(dialog, &QDialog::finished, this, &MessageForwarder::Reset);
  connect(_list, &QListWidget::itemSelectionChanged, this, [this] {
    if (_send)
      _send->setEnabled(!_pendingUid && _list->currentItem() && !_list->currentItem()->isHidden());
  });
  connect(search, &QLineEdit::textChanged, this, [this](const QString &query) {
    if (!_list)
      return;
    for (int i = 0; i < _list->count(); ++i)
      _list->item(i)->setHidden(
          !_list->item(i)->text().contains(query.left(256), Qt::CaseInsensitive));
    _send->setEnabled(!_pendingUid && _list->currentItem() && !_list->currentItem()->isHidden());
  });
  dialog->show();
}

void MessageForwarder::Choose()
{
  if (!_dialog || _pendingUid || !_list->currentItem() || _list->currentItem()->isHidden())
    return;
  if (ChatStore::instance()->Generation() != _generation ||
      UserMgr::GetInstance()->GetUid() != _uid) {
    Reset();
    return;
  }
  const int other = _list->currentItem()->data(Qt::UserRole).toInt();
  const int thread = _list->currentItem()->data(Qt::UserRole + 1).toInt();
  if (!other) {
    Send(thread, 0);
    return;
  }
  auto mgr = UserMgr::GetInstance();
  if (!mgr->CheckFriendById(other)) {
    _status->setText(tr("该好友已不在通讯录中，请重新选择"));
    return;
  }
  if (const auto data = mgr->GetChatThreadByUid(other)) {
    Send(data->GetThreadId(), other);
    return;
  }
  auto tcp = TcpMgr::GetInstance();
  if (!tcp->IsConnected() || tcp->HasPendingRequest(ID_CREATE_PRIVATE_CHAT_REQ)) {
    _status->setText(tr("连接已断开或另一个会话正在创建，请稍后重试"));
    return;
  }
  _pendingUid = other;
  _send->setEnabled(false);
  _list->setEnabled(false);
  _status->setText(tr("正在准备所选好友的会话…"));
  emit tcp->sig_send_data(ID_CREATE_PRIVATE_CHAT_REQ,
                          QJsonDocument(QJsonObject{{"uid", _uid}, {"other_id", other}})
                              .toJson(QJsonDocument::Compact));
}

void MessageForwarder::Send(int thread, int other)
{
  const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(thread);
  if (!data || data->GetOtherId() != other || !_page->ForwardSnapshots(_messages, data)) {
    _status->setText(tr("暂时无法发送，请检查连接和会话后重试"));
    _list->setEnabled(true);
    _send->setEnabled(true);
    return;
  }
  emit status(tr("正在保存待发送消息…"));
  if (_dialog)
    _dialog->accept();
}
