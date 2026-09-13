#include "chatdialog.h"
#include "chatuserwid.h"
#include "friendinfopage.h"
#include "userinfopage.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "ui_chatdialog.h"
#include <QAction>
#include <QApplication>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>

ChatDialog::ChatDialog(QWidget *parent) : QDialog(parent), ui(new Ui::ChatDialog)
{
  ui->setupUi(this);
  _friendInfoPage = new FriendInfoPage(this);
  _friendInfoPage->setObjectName("friend_info_page");
  _userInfoPage = new UserInfoPage(this);
  _userInfoPage->setObjectName("user_info_page");
  ui->stackedWidget->addWidget(_friendInfoPage);
  ui->stackedWidget->addWidget(_userInfoPage);
  ui->add_btn->SetState("normal", "hover", "press");
  ui->search_edit->SetMaxLength(27);
  ui->search_edit->setPlaceholderText(tr("搜索 uid / 用户名"));
  ui->search_edit->addAction(QIcon(":/res/search.png"), QLineEdit::LeadingPosition);
  auto *clear = ui->search_edit->addAction(QIcon(":/res/close_search.png"), QLineEdit::TrailingPosition);
  clear->setVisible(false);
  connect(clear, &QAction::triggered, ui->search_edit, &QLineEdit::clear);
  connect(ui->search_edit, &QLineEdit::textChanged, this, [this, clear](const QString &text) {
    clear->setVisible(!text.isEmpty());
    ShowSearch(!text.trimmed().isEmpty());
  });
  ui->search_list->SetSearchEdit(ui->search_edit);
  connect(ui->add_btn, &QPushButton::clicked, this, [this] { ui->search_edit->setFocus(); ShowSearch(true); });
  for (auto *button : {ui->side_chat_lb, ui->side_contact_lb}) {
    button->SetState("normal", "hover", "pressed", "selected_normal", "selected_hover", "selected_pressed");
    button->ShowRedPoint(false);
  }
  connect(ui->side_chat_lb, &StateWidget::clicked, this, [this] {
    ShowChatPage();
    if (!_threadsComplete) LoadThreads();
  });
  connect(ui->side_contact_lb, &StateWidget::clicked, this, &ChatDialog::ShowContactPage);
  ui->side_head_lb->setCursor(Qt::PointingHandCursor);
  ui->side_head_lb->setToolTip(tr("个人资料"));
  connect(_userInfoPage, &UserInfoPage::sig_avatar_changed, this, &ChatDialog::RefreshAvatar);
  connect(_userInfoPage, &UserInfoPage::sig_logout, this, &ChatDialog::sig_logout);
  connect(ui->chat_user_list, &ChatUserList::sig_loading_chat_user, this, [this] {
    AddChatUserList();
    if (!_threadsComplete) LoadThreads();
  });
  connect(ui->con_user_list, &ContactUserList::sig_loading_contact_user,
          ui->con_user_list, &ContactUserList::LoadMore);
  connect(ui->con_user_list, &ContactUserList::sig_switch_apply_friend_page, this, &ChatDialog::ShowContactPage);
  connect(ui->con_user_list, &ContactUserList::sig_switch_friend_info_page, this, [this](std::shared_ptr<UserInfo> user) {
    if (!user) return;
    _friendInfoPage->SetInfo(user);
    ui->stackedWidget->setCurrentWidget(_friendInfoPage);
    ui->search_edit->clear();
  });
  connect(_friendInfoPage, &FriendInfoPage::sig_jump_chat_item, this, &ChatDialog::OpenChat);
  connect(ui->search_list, &SearchList::sig_jump_chat_item, this, [this](std::shared_ptr<SearchInfo> user) {
    OpenChat(UserMgr::GetInstance()->GetFriendById(user->_uid));
  });
  connect(ui->friend_apply_page, &ApplyFriendPage::sig_show_search, this, [this](bool show) {
    if (!show) ui->search_edit->clear();
  });
  connect(ui->chat_user_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
    SelectThread(item->data(Qt::UserRole).toInt());
  });
  connect(ui->chat_page, &ChatPage::sig_messages_changed, this, [this](int id) { UpdateChatItem(id); });
  const auto tcp = TcpMgr::GetInstance();
  connect(tcp.get(), &TcpMgr::sig_load_chat_thread, this, &ChatDialog::ReceiveThreads);
  connect(tcp.get(), &TcpMgr::sig_load_chat_msg, this, &ChatDialog::ReceiveHistory);
  connect(tcp.get(), &TcpMgr::sig_request_failed, this, &ChatDialog::RequestFailed);
  connect(tcp.get(), &TcpMgr::sig_create_private_chat, this, [this](int uid, int other, int thread) {
    if (uid != UserMgr::GetInstance()->GetUid() || other != _creatingUser) return;
    _creatingUser = 0;
    if (!EnsureThread(thread, other)) { ui->chat_page->ShowStatus(tr("服务器返回了无效会话")); return; }
    SelectThread(thread);
  });
  connect(tcp.get(), &TcpMgr::sig_text_chat_msg, this, [this](const std::vector<std::shared_ptr<TextChatData>> &messages) {
    ReceiveMessages(messages, false);
  });
  connect(tcp.get(), &TcpMgr::sig_chat_msg_rsp, this, [this](int, const std::vector<std::shared_ptr<TextChatData>> &messages) {
    ReceiveMessages(messages, true);
  });
  connect(tcp.get(), &TcpMgr::sig_friend_apply, this, [this](std::shared_ptr<AddFriendApply> apply) {
    ui->friend_apply_page->AddNewApply(apply);
    ui->con_user_list->ShowRedPoint(true);
    ui->side_contact_lb->ShowRedPoint(true);
  });
  connect(tcp.get(), &TcpMgr::sig_auth_rsp, this, [this](std::shared_ptr<AuthRsp> auth) {
    FriendAccepted(auth->_uid, auth->_thread_id, auth->_chat_datas);
  });
  connect(tcp.get(), &TcpMgr::sig_add_auth_friend, this, [this](std::shared_ptr<AuthInfo> auth) {
    FriendAccepted(auth->_uid, auth->_thread_id, auth->_chat_datas);
  });
  qApp->installEventFilter(this);
  ShowChatPage();
}
ChatDialog::~ChatDialog() { qApp->removeEventFilter(this); delete ui; }

void ChatDialog::StartSession()
{
  ResetSession();
  RefreshAvatar();
  RefreshContacts();
  _userInfoPage->Refresh();
  ShowChatPage();
  LoadThreads();
}
void ChatDialog::ResetSession()
{
  ui->search_list->CloseFindDlg();
  const auto dialogs = findChildren<QDialog *>();
  for (auto *dialog : dialogs) dialog->reject();
  ui->search_edit->clear();
  _threadItems.clear();
  ui->chat_user_list->clear();
  ui->con_user_list->Reload();
  ui->friend_apply_page->Reload();
  _friendInfoPage->SetInfo(nullptr);
  _userInfoPage->Refresh();
  _unreadThreads.clear();
  _historyQueue.clear();
  _historyComplete.clear();
  _currentThread = _threadCursor = _historyThread = _historyCursor = _creatingUser = 0;
  _threadsLoading = _threadsComplete = _addingItems = false;
  ui->chat_page->Reset();
  ui->side_chat_lb->ShowRedPoint(false);
  ui->side_contact_lb->ShowRedPoint(false);
}
void ChatDialog::RefreshAvatar()
{
  QPixmap icon(UserMgr::GetInstance()->GetIcon());
  if (icon.isNull()) icon.load(":/res/head_1.jpg");
  ui->side_head_lb->setPixmap(icon.scaled(ui->side_head_lb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  if (_currentThread > 0) ui->chat_page->SetChatData(UserMgr::GetInstance()->GetChatThreadByThreadId(_currentThread));
}
void ChatDialog::RefreshContacts()
{
  ui->con_user_list->Reload();
  ui->friend_apply_page->Reload();
  bool pending = false;
  for (const auto &apply : UserMgr::GetInstance()->GetApplyList()) pending |= apply->_status == 0;
  ui->con_user_list->ShowRedPoint(pending);
  ui->side_contact_lb->ShowRedPoint(pending);
}

void ChatDialog::ShowSearch(bool show)
{
  ui->search_list->setVisible(show);
  ui->chat_user_list->setVisible(!show && _state == ChatMode);
  ui->con_user_list->setVisible(!show && _state == ContactMode);
}
void ChatDialog::ShowChatPage()
{
  _state = ChatMode;
  ui->side_contact_lb->ClearState();
  ui->side_chat_lb->SetSelected(true);
  ui->stackedWidget->setCurrentWidget(ui->chat_page);
  ui->search_edit->clear();
  ShowSearch(false);
  if (_currentThread > 0) {
    _unreadThreads.remove(_currentThread);
    UpdateChatItem(_currentThread);
  }
}
void ChatDialog::ShowContactPage()
{
  _state = ContactMode;
  ui->side_chat_lb->ClearState();
  ui->side_contact_lb->SetSelected(true);
  ui->stackedWidget->setCurrentWidget(ui->friend_apply_page);
  ui->side_contact_lb->ShowRedPoint(false);
  ui->con_user_list->ShowRedPoint(false);
  ui->search_edit->clear();
  ShowSearch(false);
}

bool ChatDialog::eventFilter(QObject *watched, QEvent *event)
{
  if (event->type() != QEvent::MouseButtonPress || !isVisible()) return QDialog::eventFilter(watched, event);
  if (watched == ui->side_head_lb) {
    _userInfoPage->Refresh();
    ui->stackedWidget->setCurrentWidget(_userInfoPage);
    ui->search_edit->clear();
    return true;
  }
  const auto *widget = qobject_cast<QWidget *>(watched);
  if (widget && isAncestorOf(widget) && widget->window() == window() && ui->search_list->isVisible()) {
    const auto pos = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
    if (!ui->search_edit->rect().contains(ui->search_edit->mapFromGlobal(pos))
        && !ui->search_list->rect().contains(ui->search_list->mapFromGlobal(pos))
        && !ui->add_btn->rect().contains(ui->add_btn->mapFromGlobal(pos))) {
      ui->search_edit->clear();
      ShowSearch(false);
    }
  }
  return QDialog::eventFilter(watched, event);
}

std::shared_ptr<ChatThreadData> ChatDialog::EnsureThread(int threadId, int otherUid)
{
  if (threadId <= 0 || otherUid <= 0 || otherUid == UserMgr::GetInstance()->GetUid()) return nullptr;
  const auto mgr = UserMgr::GetInstance();
  auto data = mgr->GetChatThreadByThreadId(threadId);
  if (!data) {
    data = std::make_shared<ChatThreadData>(otherUid, threadId, 0);
    mgr->AddChatThreadData(data, otherUid);
  }
  return data;
}
void ChatDialog::LoadThreads()
{
  if (_threadsLoading || _threadsComplete || !TcpMgr::GetInstance()->IsConnected()) return;
  _threadsLoading = true;
  const QJsonObject request{{"uid", UserMgr::GetInstance()->GetUid()}, {"thread_id", _threadCursor}};
  emit TcpMgr::GetInstance()->sig_send_data(ID_LOAD_CHAT_THREAD_REQ, QJsonDocument(request).toJson(QJsonDocument::Compact));
}
void ChatDialog::ReceiveThreads(bool more, int lastId, std::vector<std::shared_ptr<ChatThreadInfo>> threads)
{
  if (!_threadsLoading) return;
  _threadsLoading = false;
  for (const auto &thread : threads) {
    if (thread->_type != "private") continue; // 参考项目群聊仅有占位，尚无完整实现。
    const int self = UserMgr::GetInstance()->GetUid();
    if (thread->_user1_id != self && thread->_user2_id != self) continue;
    const int other = thread->_user1_id == self ? thread->_user2_id : thread->_user1_id;
    EnsureThread(thread->_thread_id, other);
  }
  if (_threadItems.size() < CHAT_COUNT_PER_PAGE) {
    AddChatUserList(CHAT_COUNT_PER_PAGE - _threadItems.size());
  }
  if (_currentThread == 0 && !_threadItems.isEmpty()) SelectThread(_threadItems.firstKey());
  if (more && lastId <= _threadCursor) {
    ui->chat_page->ShowStatus(tr("会话分页游标未前进，请重新登录后重试"));
    return;
  }
  _threadCursor = lastId;
  _threadsComplete = !more;
  if (more) QTimer::singleShot(0, this, &ChatDialog::LoadThreads);
}
void ChatDialog::AddChatUserList(int limit)
{
  if (_addingItems) return;
  _addingItems = true;
  int count = 0;
  for (auto it = UserMgr::GetInstance()->GetChatThreads().cbegin();
       it != UserMgr::GetInstance()->GetChatThreads().cend(); ++it) {
    if (_threadItems.contains(it.key())) continue;
    UpdateChatItem(it.key());
    if (++count == limit) break;
  }
  _addingItems = false;
}
void ChatDialog::UpdateChatItem(int threadId, bool addIfMissing)
{
  const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId);
  if (!data) return;
  auto *item = _threadItems.value(threadId, nullptr);
  if (!item && !addIfMissing) return;
  if (!item) {
    const bool wasAdding = _addingItems;
    _addingItems = true;
    auto *widget = new ChatUserWid;
    item = new QListWidgetItem;
    item->setData(Qt::UserRole, threadId);
    item->setSizeHint(widget->sizeHint());
    _threadItems.insert(threadId, item);
    ui->chat_user_list->addItem(item);
    ui->chat_user_list->setItemWidget(item, widget);
    _addingItems = wasAdding;
  }
  auto *widget = qobject_cast<ChatUserWid *>(ui->chat_user_list->itemWidget(item));
  widget->SetChatData(data);
  widget->ShowRedPoint(_unreadThreads.contains(threadId));
  ui->side_chat_lb->ShowRedPoint(!_unreadThreads.isEmpty());
}
void ChatDialog::OpenChat(std::shared_ptr<UserInfo> user)
{
  if (!user || user->_uid == UserMgr::GetInstance()->GetUid()) return;
  if (auto data = UserMgr::GetInstance()->GetChatThreadByUid(user->_uid)) {
    SelectThread(data->GetThreadId());
    return;
  }
  ShowChatPage();
  if (_creatingUser != 0) { ui->chat_page->ShowStatus(tr("正在创建会话，请稍候")); return; }
  _creatingUser = user->_uid;
  const QJsonObject request{{"uid", UserMgr::GetInstance()->GetUid()}, {"other_id", user->_uid}};
  emit TcpMgr::GetInstance()->sig_send_data(ID_CREATE_PRIVATE_CHAT_REQ, QJsonDocument(request).toJson(QJsonDocument::Compact));
}
void ChatDialog::SelectThread(int threadId)
{
  const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId);
  if (!data) return;
  _currentThread = threadId;
  _unreadThreads.remove(threadId);
  UpdateChatItem(threadId);
  ui->chat_user_list->setCurrentItem(_threadItems.value(threadId));
  ui->chat_page->SetChatData(data);
  ui->chat_page->ShowStatus({});
  ShowChatPage();
  QueueHistory(threadId);
}

void ChatDialog::QueueHistory(int threadId)
{
  if (_historyComplete.contains(threadId) || _historyThread == threadId || _historyQueue.contains(threadId)) return;
  _historyQueue.enqueue(threadId);
  LoadNextHistory();
}
void ChatDialog::LoadNextHistory()
{
  if (_historyThread != 0 || _historyQueue.isEmpty() || !TcpMgr::GetInstance()->IsConnected()) return;
  _historyThread = _historyQueue.dequeue();
  _historyCursor = 0;
  SendHistoryRequest();
}
void ChatDialog::SendHistoryRequest()
{
  const QJsonObject request{{"thread_id", _historyThread}, {"message_id", _historyCursor}};
  emit TcpMgr::GetInstance()->sig_send_data(ID_LOAD_CHAT_MSG_REQ, QJsonDocument(request).toJson(QJsonDocument::Compact));
}
void ChatDialog::ReceiveHistory(int threadId, int messageId, bool more, std::vector<std::shared_ptr<TextChatData>> messages)
{
  if (threadId != _historyThread) return;
  auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId);
  if (!data) {
    _historyThread = 0;
    LoadNextHistory();
    return;
  }
  for (const auto &message : messages) {
    if (message->GetThreadId() == threadId) data->MoveMsg(message);
  }
  UpdateChatItem(threadId, false);
  if (_currentThread == threadId) ui->chat_page->SetChatData(data);
  if (more && messageId <= _historyCursor) {
    RequestFailed(ID_LOAD_CHAT_MSG_REQ, tr("聊天记录分页游标未前进，请重新打开会话重试"));
    return;
  }
  _historyCursor = messageId;
  if (more) { SendHistoryRequest(); return; }
  _historyComplete.insert(threadId);
  _historyThread = 0;
  LoadNextHistory();
}

void ChatDialog::ReceiveMessages(const std::vector<std::shared_ptr<TextChatData>> &messages, bool reply)
{
  for (const auto &message : messages) {
    const int thread = message->GetThreadId();
    auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(thread);
    if (!data && !reply) data = EnsureThread(thread, message->GetSendUid());
    if (!data) continue;
    const bool known = data->GetMsgMapRef().contains(message->GetMsgId());
    const bool pending = data->GetMsgUnRspRef().contains(message->GetUniqueId());
    data->MoveMsg(message);
    if (_currentThread == thread) {
      if (pending) ui->chat_page->UpdateChatStatus(message->GetUniqueId(), message->GetStatus());
      else if (!known) ui->chat_page->AppendChatMsg(message);
    }
    const bool visible = _currentThread == thread && ui->stackedWidget->currentWidget() == ui->chat_page;
    if (!reply && !visible && !known) _unreadThreads.insert(thread);
    UpdateChatItem(thread);
  }
}
void ChatDialog::FriendAccepted(int uid, int threadId, const std::vector<std::shared_ptr<TextChatData>> &messages)
{
  // 等认证对话框保存本地备注后，再刷新列表中的显示文本。
  QTimer::singleShot(0, this, &ChatDialog::RefreshContacts);
  if (auto data = EnsureThread(threadId, uid)) {
    ReceiveMessages(messages, false);
    UpdateChatItem(threadId);
    QueueHistory(threadId);
  } else {
    // 没有初始消息时，不从空数组取会话 ID；由会话接口重新获取。
    _threadsComplete = false;
    LoadThreads();
  }
}
void ChatDialog::RequestFailed(ReqId request, const QString &message)
{
  if (request == ID_LOAD_CHAT_THREAD_REQ) _threadsLoading = false;
  else if (request == ID_LOAD_CHAT_MSG_REQ) {
    _historyThread = 0;
    QTimer::singleShot(0, this, &ChatDialog::LoadNextHistory);
  } else if (request == ID_CREATE_PRIVATE_CHAT_REQ) _creatingUser = 0;
  else if (request == ID_TEXT_CHAT_MSG_REQ) {
    for (const auto &thread : UserMgr::GetInstance()->GetChatThreads()) {
      for (const auto &pending : thread->GetMsgUnRspRef()) {
        pending->SetStatus(SEND_FAILED);
        if (thread->GetThreadId() == _currentThread) ui->chat_page->UpdateChatStatus(pending->GetUniqueId(), SEND_FAILED);
      }
    }
  } else return;
  ui->chat_page->ShowStatus(message);
}
