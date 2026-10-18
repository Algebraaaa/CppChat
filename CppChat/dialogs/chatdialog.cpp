#include "chatdialog.h"
#include "addfrienddialog.h"
#include "chatpage.h"
#include "common/avatarimage.h"
#include "common/chatstore.h"
#include "common/conversationpreferences.h"
#include "groupdialog.h"
#include "network/resourcemgr.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "profiledialog.h"
#include "quickactionmenu.h"
#include "services/chatsyncservice.h"
#include "sidemenupopup.h"
#include "ui_chatdialog.h"
#include <QApplication>
#include <QButtonGroup>
#include <QEvent>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QToolTip>
#include <utility>

ChatDialog::ChatDialog(QWidget *parent) : QDialog(parent), ui(new Ui::ChatDialog)
{
  ui->setupUi(this);
  _sync = new ChatSyncService(this);
  _profileDialog = new ProfileDialog(this);
  connect(_profileDialog, &ProfileDialog::sig_profile_changed, this, &ChatDialog::RefreshAvatar);
  connect(_profileDialog, &ProfileDialog::sig_logout, this, &ChatDialog::sig_logout);
  connect(ui->conversation_section, &ConversationSection::sig_open_friend, this,
          &ChatDialog::OpenChat);
  connect(ui->conversation_section, &ConversationSection::sig_open_thread, this,
          &ChatDialog::SelectThread);
  connect(ui->conversation_section, &ConversationSection::sig_thread_unread_changed, this,
          [this](int threadId, bool unread) { SetThreadUnread(threadId, unread, true); });
  connect(ui->conversation_section, &ConversationSection::sig_load_more, this, [this] {
    AddChatUserList();
    if (!_sync->ThreadsComplete())
      _sync->LoadThreads();
  });
  connect(ui->conversation_section, &ConversationSection::sig_search_history_requested, this,
          [this] {
            // 复用现有分页协议补齐搜索历史；搜索本身由聊天模块负责。
            _sync->RefreshAllHistory();
          });
  connect(ui->contacts_section, &ContactsSection::sig_open_chat, this, &ChatDialog::OpenChat);
  connect(ui->contacts_section, &ContactsSection::sig_open_group, this, &ChatDialog::SelectThread);
  connect(ui->contacts_section, &ContactsSection::sig_unread_changed, ui->contact_unread_dot,
          &QWidget::setVisible);

  auto *quickMenu = new QuickActionMenu(this);
  const auto openGroup = [this](int thread) {
    for (auto *existing : findChildren<GroupDialog *>())
      if (existing->ThreadId() == thread) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
      } else {
        existing->close(); // One correlated membership request at a time across dialogs.
      }
    auto *dialog = new GroupDialog(thread, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &GroupDialog::groupOpened, this, &ChatDialog::SelectThread);
    dialog->open();
  };
  connect(quickMenu, &QuickActionMenu::createGroupRequested, this, [openGroup] { openGroup(0); });
  connect(ui->conversation_section->Chat(), &ChatPage::sig_group_settings, this, openGroup);
  connect(_sync, &ChatSyncService::groupRemoved, this, [this](int id) {
    ui->contacts_section->RefreshGroups();
    ui->conversation_section->RemoveThread(id);
    _unreadThreads.remove(id);
    _manuallyUnreadThreads.remove(id);
    if (_currentThread == id) {
      _currentThread = 0;
      ui->conversation_section->Chat()->ShowStatus(tr("你已退出群聊，或该群已被解散"));
    }
    ui->chat_unread_dot->setVisible(!_unreadThreads.isEmpty());
  });
  connect(quickMenu, &QuickActionMenu::placeholderRequested, this, [this](const QString &title) {
    QMessageBox::information(this, tr("功能建设中"), tr("%1功能暂未实现。").arg(title));
  });
  connect(quickMenu, &QuickActionMenu::addFriendRequested, this, [this] {
    if (_addFriendDialog) {
      _addFriendDialog->show();
      _addFriendDialog->raise();
      _addFriendDialog->activateWindow();
      return;
    }

    auto *dialog = new AddFriendDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    _addFriendDialog = dialog;
    connect(dialog, &AddFriendDialog::sig_jump_chat_item, this,
            [this](const std::shared_ptr<SearchInfo> &user) {
              if (user)
                OpenChat(UserMgr::GetInstance()->GetFriendById(user->_uid));
            });
    dialog->open();
  });
  connect(ui->conversation_section, &ConversationSection::sig_quick_actions_requested, this,
          [this, quickMenu] {
            quickMenu->ensurePolished();
            quickMenu->Popup(ui->conversation_section->QuickMenuPosition(quickMenu->sizeHint()));
          });
  // 导航按钮互斥选中；普通按钮状态交给 Qt 和 QSS 管理。
  auto *navigationButtons = new QButtonGroup(this);
  navigationButtons->setExclusive(true);
  navigationButtons->addButton(ui->side_chat_btn);
  navigationButtons->addButton(ui->side_contact_btn);
  navigationButtons->addButton(ui->side_favorites_btn);
  navigationButtons->addButton(ui->side_discover_btn);
  for (auto *dot : {ui->chat_unread_dot, ui->contact_unread_dot}) {
    dot->setAttribute(Qt::WA_TransparentForMouseEvents);
    dot->hide();
  }
  auto *phoneButton = ui->side_phone_btn;
  phoneButton->setAccessibleName(phoneButton->toolTip());
  connect(phoneButton, &QPushButton::clicked, this, [phoneButton] {
    QToolTip::showText(phoneButton->mapToGlobal(QPoint(phoneButton->width() + 8, 0)),
                       phoneButton->toolTip(), phoneButton);
  });
  _sideMenu = new SideMenuPopup(this);
  ui->side_menu_btn->setAccessibleName(tr("更多菜单"));
  connect(ui->side_menu_btn, &QPushButton::clicked, this, [this] {
    ++_navigationRevision;
    _sideMenu->PopupAt(ui->side_menu_btn);
  });
  connect(_sideMenu, &SideMenuPopup::sig_action_requested, this, [this](const QString &title) {
    QMessageBox::information(this, title, tr("%1功能暂未实现，当前仅提供界面占位。").arg(title));
  });
  connect(ui->side_chat_btn, &QPushButton::clicked, this, [this] {
    ShowChatPage();
    if (!_sync->ThreadsComplete())
      _sync->LoadThreads();
    else if (_currentThread == 0 && _creatingUser == 0 &&
             ui->conversation_section->ThreadCount() > 0)
      SelectThread(ui->conversation_section->FirstThreadId());
  });
  connect(ui->side_contact_btn, &QPushButton::clicked, this, &ChatDialog::ShowContactPage);
  connect(ui->side_favorites_btn, &QPushButton::clicked, this, &ChatDialog::ShowFavoritesPage);
  connect(ui->conversation_section->Chat(), &ChatPage::sig_open_favorites, this,
          &ChatDialog::ShowFavoritesPage);
  connect(ui->side_discover_btn, &QPushButton::clicked, this, &ChatDialog::ShowDiscoverPage);
  ui->side_head_btn->setCursor(Qt::PointingHandCursor);
  ui->side_head_btn->setToolTip(tr("个人资料"));
  ui->side_head_btn->setAccessibleName(tr("个人资料"));
  connect(ui->side_head_btn, &QPushButton::clicked, this, [this] {
    ++_navigationRevision; // 打开全局窗口也是新的用户意图，旧请求不能随后抢走页面。
    if (!_profileDialog->isVisible())
      _profileDialog->Refresh();
    _profileDialog->show();
    _profileDialog->raise();
    _profileDialog->activateWindow();
  });
  connect(UserMgr::GetInstance().get(), &UserMgr::sig_avatar_ready, this, [this](int uid) {
    if (uid == UserMgr::GetInstance()->GetUid())
      RefreshAvatar();
  });
  connect(ui->conversation_section->Chat(), &ChatPage::sig_messages_changed, this,
          [this](int id) { UpdateChatItem(id); });
  connect(ChatStore::instance(), &ChatStore::storageError, this, [this](const QString &error) {
    ui->conversation_section->Chat()->ShowStatus(tr("本地聊天记录保存失败：%1").arg(error));
  });
  connect(ui->conversation_section->Chat(), &ChatPage::sig_reload_local, _sync,
          &ChatSyncService::LoadLocalMessages);
  const auto tcp = TcpMgr::GetInstance();
  connect(tcp.get(), &TcpMgr::sig_contact_tags_updated, this, [this](int uid) {
    RefreshContacts();
    const auto thread = UserMgr::GetInstance()->GetChatThreadByUid(uid);
    if (!thread)
      return;
    UpdateChatItem(thread->GetThreadId(), false);
    if (_currentThread == thread->GetThreadId())
      ui->conversation_section->Chat()->SetChatData(thread);
  });
  connect(tcp.get(), &TcpMgr::sig_request_failed, this, &ChatDialog::RequestFailed);
  connect(_sync, &ChatSyncService::conversationsChanged, this,
          &ChatDialog::RefreshConversationList);
  connect(_sync, &ChatSyncService::messagesChanged, this, [this](int threadId) {
    UpdateChatItem(threadId);
    if (_currentThread == threadId)
      ui->conversation_section->Chat()->SetChatData(
          UserMgr::GetInstance()->GetChatThreadByThreadId(threadId));
  });
  connect(_sync, &ChatSyncService::incomingMessage, this, [this](int threadId) {
    SetThreadUnread(threadId, !(_currentThread == threadId && IsChatVisible()));
  });
  connect(_sync, &ChatSyncService::status, ui->conversation_section->Chat(), &ChatPage::ShowStatus);
  connect(_sync, &ChatSyncService::initializationFailed, this, [this](const QString &error) {
    QMessageBox::warning(this, tr("本地聊天记录初始化失败"),
                         tr("聊天记录尚未加载：%1\n请检查上述问题并重新登录。").arg(error));
  });
  connect(tcp.get(), &TcpMgr::sig_friend_deleted, this, &ChatDialog::FriendDeleted);
  connect(tcp.get(), &TcpMgr::sig_create_private_chat, this,
          [this](int uid, int other, int thread) {
            if (uid != UserMgr::GetInstance()->GetUid() || other != _creatingUser)
              return;
            const bool shouldOpen = _createNavigationRevision == _navigationRevision &&
                                    _currentSection == Section::Chat;
            _creatingUser = 0;
            if (!_sync->EnsureThread(thread, other)) {
              ui->conversation_section->Chat()->ShowStatus(tr("服务器返回了无效会话"));
              return;
            }
            UpdateChatItem(thread);
            _sync->QueueHistory(thread);
            if (shouldOpen)
              SelectThread(thread);
          });
  connect(tcp.get(), &TcpMgr::sig_friend_apply, this,
          [this](std::shared_ptr<AddFriendApply> apply) { ui->contacts_section->AddApply(apply); });
  connect(tcp.get(), &TcpMgr::sig_auth_rsp, this, [this](std::shared_ptr<AuthRsp> auth) {
    FriendAccepted(auth->_uid, auth->_thread_id, auth->_chat_datas);
  });
  connect(tcp.get(), &TcpMgr::sig_add_auth_friend, this, [this](std::shared_ptr<AuthInfo> auth) {
    FriendAccepted(auth->_uid, auth->_thread_id, auth->_chat_datas);
  });
  qApp->installEventFilter(this);
  ShowChatPage();
}
ChatDialog::~ChatDialog()
{
  qApp->removeEventFilter(this);
  delete ui;
}

void ChatDialog::StartSession()
{
  ResetSession();
  const auto preferences = ConversationPreferences::Load(UserMgr::GetInstance()->GetUid());
  _unreadThreads = preferences.unread;
  ui->conversation_section->SetPinnedThreads(preferences.pinned);
  ui->chat_unread_dot->setVisible(!_unreadThreads.isEmpty());
  RefreshAvatar();
  RefreshContacts();
  _profileDialog->Refresh();
  ShowChatPage();
  _sync->StartSession(UserMgr::GetInstance()->GetUid(), gate_url_prefix);
}
void ChatDialog::ResetSession()
{
  ConversationPreferences::Flush();
  ++_navigationRevision;
  const auto dialogs = findChildren<QDialog *>();
  for (auto *dialog : dialogs)
    dialog->reject();
  _profileDialog->Reset();
  ui->conversation_section->Reset();
  _sync->ResetSession();
  ResourceMgr::instance()->ResetSession();
  ui->contacts_section->Reset();
  ui->favorites_page->Reset();
  ui->discover_page->Reset();
  _unreadThreads.clear();
  _manuallyUnreadThreads.clear();
  _currentThread = _creatingUser = 0;
  ui->chat_unread_dot->hide();
  ui->contact_unread_dot->hide();
  SwitchSection(Section::Chat);
}
void ChatDialog::RefreshAvatar()
{
  QPixmap icon = UserMgr::GetInstance()->AvatarPixmap(UserMgr::GetInstance()->GetUid(),
                                                      UserMgr::GetInstance()->GetIcon());
  ui->side_head_btn->setIcon(QIcon(roundedAvatarPixmap(icon, ui->side_head_btn->size())));
  ui->side_head_btn->setIconSize(ui->side_head_btn->size());
  if (_currentThread > 0)
    ui->conversation_section->Chat()->SetChatData(
        UserMgr::GetInstance()->GetChatThreadByThreadId(_currentThread));
}
void ChatDialog::RefreshContacts()
{
  ui->contacts_section->Refresh();
  ui->conversation_section->RefreshSearch();
  if (const auto current = UserMgr::GetInstance()->GetChatThreadByThreadId(_currentThread)) {
    UpdateChatItem(_currentThread);
    ui->conversation_section->Chat()->SetChatData(current);
  }
}

void ChatDialog::SwitchSection(Section section)
{
  ++_navigationRevision;
  _currentSection = section;
  // 一个入口统一更新页面与按钮。各模块实例常驻，切换不清空搜索、草稿和详情。
  switch (section) {
  case Section::Chat:
    ui->section_stack->setCurrentWidget(ui->conversation_section);
    ui->side_chat_btn->setChecked(true);
    break;
  case Section::Contacts:
    ui->section_stack->setCurrentWidget(ui->contacts_section);
    ui->side_contact_btn->setChecked(true);
    break;
  case Section::Favorites:
    ui->section_stack->setCurrentWidget(ui->favorites_page);
    ui->side_favorites_btn->setChecked(true);
    break;
  case Section::Discover:
    ui->section_stack->setCurrentWidget(ui->discover_page);
    ui->side_discover_btn->setChecked(true);
    break;
  }
}

bool ChatDialog::IsChatVisible() const
{
  return _currentSection == Section::Chat && ui->conversation_section->isVisible() &&
         window()->isActiveWindow() && !window()->isMinimized();
}

void ChatDialog::MarkCurrentRead()
{
  // 菜单收起也会产生 WindowActivate，不能抹掉刚设置的手动未读。
  if (_currentThread > 0 && IsChatVisible() && !_manuallyUnreadThreads.contains(_currentThread))
    SetThreadUnread(_currentThread, false);
}

void ChatDialog::SetThreadUnread(int threadId, bool unread, bool manuallyMarked)
{
  if (!UserMgr::GetInstance()->GetChatThreadByThreadId(threadId))
    return;
  const bool wasUnread = _unreadThreads.contains(threadId);
  if (unread) {
    _unreadThreads.insert(threadId);
    if (manuallyMarked)
      _manuallyUnreadThreads.insert(threadId);
  } else {
    _unreadThreads.remove(threadId);
    _manuallyUnreadThreads.remove(threadId);
  }
  if (wasUnread != unread)
    ConversationPreferences::SaveUnread(UserMgr::GetInstance()->GetUid(), _unreadThreads);
  if (manuallyMarked)
    ConversationPreferences::Flush();
  UpdateChatItem(threadId);
}

void ChatDialog::ShowChatPage()
{
  SwitchSection(Section::Chat);
  MarkCurrentRead();
}
void ChatDialog::ShowContactPage() { SwitchSection(Section::Contacts); }
void ChatDialog::ShowFavoritesPage() { SwitchSection(Section::Favorites); }
void ChatDialog::ShowDiscoverPage() { SwitchSection(Section::Discover); }

bool ChatDialog::eventFilter(QObject *watched, QEvent *event)
{
  // 恢复窗口焦点后再判断可见性；搜索事件分别由两个业务模块处理。
  if (watched == window() && event->type() == QEvent::WindowActivate)
    QTimer::singleShot(0, this, &ChatDialog::MarkCurrentRead);
  return QDialog::eventFilter(watched, event);
}

void ChatDialog::RefreshConversationList(bool complete)
{
  ui->contacts_section->RefreshGroups();
  if (auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(_currentThread)) {
    if (data->IsGroup()) {
      UpdateChatItem(_currentThread);
      ui->conversation_section->Chat()->SetChatData(data);
    }
  }
  if (!complete) {
    AddChatUserList();
    return;
  }
  // 会话全集到齐后清理本机残留标记，避免已删除好友留下侧栏红点。
  QSet<int> availableThreads;
  for (const auto &data : UserMgr::GetInstance()->GetChatThreads())
    if (data)
      availableThreads.insert(data->GetThreadId());
  const auto oldUnread = _unreadThreads;
  _unreadThreads.intersect(availableThreads);
  _manuallyUnreadThreads.intersect(availableThreads);
  const int uid = UserMgr::GetInstance()->GetUid();
  if (oldUnread != _unreadThreads)
    ConversationPreferences::SaveUnread(uid, _unreadThreads);
  auto preferences = ConversationPreferences::Load(uid);
  const auto oldPinned = preferences.pinned;
  preferences.pinned.intersect(availableThreads);
  if (oldPinned != preferences.pinned)
    ConversationPreferences::SavePinned(uid, preferences.pinned);
  ui->conversation_section->SetPinnedThreads(preferences.pinned);
  ui->chat_unread_dot->setVisible(!_unreadThreads.isEmpty());
  AddChatUserList();
  if (_currentThread == 0 && _creatingUser == 0 && ui->conversation_section->ThreadCount() > 0 &&
      _currentSection == Section::Chat)
    SelectThread(ui->conversation_section->FirstThreadId());
}

void ChatDialog::AddChatUserList(int limit)
{
  ui->conversation_section->AddThreads(_unreadThreads, limit);
}

void ChatDialog::UpdateChatItem(int threadId, bool addIfMissing)
{
  ui->conversation_section->UpdateThread(UserMgr::GetInstance()->GetChatThreadByThreadId(threadId),
                                         _unreadThreads.contains(threadId), addIfMissing);
  ui->chat_unread_dot->setVisible(!_unreadThreads.isEmpty());
}

void ChatDialog::OpenChat(std::shared_ptr<UserInfo> user)
{
  if (!user || user->_uid == UserMgr::GetInstance()->GetUid())
    return;
  if (auto data = UserMgr::GetInstance()->GetChatThreadByUid(user->_uid)) {
    SelectThread(data->GetThreadId());
    return;
  }
  ShowChatPage();
  if (_creatingUser != 0) {
    ui->conversation_section->Chat()->ShowStatus(tr("正在创建会话，请稍候"));
    return;
  }
  _creatingUser = user->_uid;
  _createNavigationRevision = _navigationRevision;
  const QJsonObject request{{"uid", UserMgr::GetInstance()->GetUid()}, {"other_id", user->_uid}};
  emit TcpMgr::GetInstance() -> sig_send_data(
      ID_CREATE_PRIVATE_CHAT_REQ, QJsonDocument(request).toJson(QJsonDocument::Compact));
}
void ChatDialog::SelectThread(int threadId)
{
  const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId);
  if (!data)
    return;
  _manuallyUnreadThreads.remove(threadId);
  _currentThread = threadId;
  UpdateChatItem(threadId);
  ui->conversation_section->SelectThread(data);
  ShowChatPage();
  _sync->LoadLocalMessages(threadId);
  _sync->QueueHistory(threadId, true);
}

void ChatDialog::FriendAccepted(int uid, int threadId,
                                const std::vector<std::shared_ptr<TextChatData>> &messages)
{
  // 等认证对话框保存本地备注后，再刷新列表中的显示文本。
  QTimer::singleShot(0, this, &ChatDialog::RefreshContacts);
  if (auto data = _sync->EnsureThread(threadId, uid)) {
    _sync->ReceiveMessages(messages, false);
    UpdateChatItem(threadId);
    _sync->QueueHistory(threadId);
  } else {
    // 没有初始消息时，不从空数组取会话 ID；由会话接口重新获取。
    _sync->ReloadThreads();
  }
}
void ChatDialog::FriendDeleted(int uid)
{
  const int threadId = UserMgr::GetInstance()->RemoveFriend(uid);
  ui->contacts_section->FriendRemoved(uid);
  RefreshContacts();
  if (_creatingUser == uid) {
    TcpMgr::GetInstance()->CancelRequest(ID_CREATE_PRIVATE_CHAT_REQ);
    _creatingUser = 0;
  }
  if (threadId <= 0)
    return;
  _sync->RemoveThread(threadId);
  _unreadThreads.remove(threadId);
  _manuallyUnreadThreads.remove(threadId);
  ui->conversation_section->RemoveThread(threadId);
  if (_currentThread == threadId) {
    _currentThread = 0;
    ui->conversation_section->Chat()->ShowStatus(tr("好友关系已解除，重新添加好友后可继续聊天"));
  }
  ui->chat_unread_dot->setVisible(!_unreadThreads.isEmpty());
}
void ChatDialog::RequestFailed(ReqId request, const QString &message)
{
  if (request != ID_CREATE_PRIVATE_CHAT_REQ)
    return;
  _creatingUser = 0;
  ui->conversation_section->Chat()->ShowStatus(message);
}
