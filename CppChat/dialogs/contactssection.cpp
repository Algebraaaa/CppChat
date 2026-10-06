#include "contactssection.h"
#include "network/usermgr.h"
#include "ui_contactssection.h"
#include <QAction>
#include <QApplication>
#include <QMessageBox>
#include <QMouseEvent>

ContactsSection::ContactsSection(QWidget *parent) : QWidget(parent), ui(new Ui::ContactsSection)
{
  ui->setupUi(this);
  setAttribute(Qt::WA_StyledBackground);
  ui->contacts_list_wid->setAttribute(Qt::WA_StyledBackground);
  ui->contact_placeholder_page->setAttribute(Qt::WA_StyledBackground);
  ui->contact_splitter->setStretchFactor(0, 0);
  ui->contact_splitter->setStretchFactor(1, 1);
  ui->contact_splitter->setSizes({300, 820});
  ui->contact_manage_btn->setIcon(QIcon(":/icons/action-group-chat.svg"));
  ui->placeholder_icon->setPixmap(QIcon(":/icons/action-group-chat.svg").pixmap(44, 44));
  ui->search_edit->SetMaxLength(27);
  ui->search_edit->setPlaceholderText(tr("搜索"));
  ui->search_edit->setToolTip(tr("搜索已添加的联系人"));
  ui->search_edit->addAction(QIcon(":/res/search.png"), QLineEdit::LeadingPosition);
  auto *clear =
      ui->search_edit->addAction(QIcon(":/res/close_search.png"), QLineEdit::TrailingPosition);
  clear->setVisible(false);
  _searchTimer.setSingleShot(true);
  connect(&_searchTimer, &QTimer::timeout, this, [this] {
    if (!ui->search_edit->text().trimmed().isEmpty())
      ui->search_list->Search(ui->search_edit->text(), SearchList::Scope::Contacts);
  });
  connect(clear, &QAction::triggered, ui->search_edit, &QLineEdit::clear);
  connect(ui->search_edit, &QLineEdit::textChanged, this, [this, clear](const QString &text) {
    clear->setVisible(!text.isEmpty());
    _searchTimer.stop();
    ui->search_list->ClearResults();
    ShowSearch(!text.trimmed().isEmpty());
    if (!text.trimmed().isEmpty())
      _searchTimer.start(200);
  });
  connect(ui->search_list, &SearchList::sig_open_friend, this, &ContactsSection::ShowFriend);
  connect(ui->con_user_list, &ContactUserList::sig_switch_friend_info_page, this,
          &ContactsSection::ShowFriend);
  connect(ui->con_user_list, &ContactUserList::sig_switch_apply_friend_page, this,
          &ContactsSection::ShowNewFriends);
  connect(ui->con_user_list, &ContactUserList::sig_open_friend_apply, this,
          &ContactsSection::ShowApplication);
  connect(ui->con_user_list, &ContactUserList::sig_show_placeholder, this,
          &ContactsSection::ShowPlaceholder);
  connect(ui->contact_manage_btn, &QPushButton::clicked, this, [this] {
    ui->con_user_list->setCurrentItem(nullptr);
    ShowPlaceholder(tr("通讯录管理"),
                    tr("通讯录管理功能暂未开放\n删除好友可在好友资料的更多菜单中操作"));
  });
  connect(ui->friend_info_page, &FriendInfoPage::sig_jump_chat_item, this,
          &ContactsSection::sig_open_chat);
  connect(ui->friend_info_page, &FriendInfoPage::sig_show_placeholder, this,
          [this](const QString &title, const QString &description) {
            QMessageBox::information(this, title, description);
          });
  connect(ui->friend_apply_page, &ApplyFriendPage::sig_show_search, this, [this](bool show) {
    if (!show)
      ui->search_edit->clear();
  });
  ShowSearch(false);
  qApp->installEventFilter(this);
}

ContactsSection::~ContactsSection()
{
  qApp->removeEventFilter(this);
  delete ui;
}

void ContactsSection::ShowSearch(bool show)
{
  ui->search_list->setVisible(show);
  ui->con_user_list->setVisible(!show);
  ui->contact_manage_btn->setVisible(!show);
  ui->management_layout->setContentsMargins(14, 0, 14, show ? 0 : 16);
}

void ContactsSection::ShowFriend(std::shared_ptr<UserInfo> user)
{
  if (!user)
    return;
  ui->friend_info_page->SetInfo(user);
  ui->contact_detail_stack->setCurrentWidget(ui->friend_info_page);
  ui->search_edit->clear();
}

void ContactsSection::ShowNewFriends()
{
  ShowPlaceholder(tr("新的朋友"), tr("选择左侧好友申请查看详情"));
  ui->con_user_list->SelectNewFriends();
  ui->con_user_list->ShowRedPoint(false);
  ui->search_edit->clear();
  emit sig_unread_changed(false);
}

void ContactsSection::ShowApplication(std::shared_ptr<ApplyInfo> apply)
{
  if (!apply)
    return;
  ui->search_edit->clear();
  ui->con_user_list->ShowRedPoint(false);
  emit sig_unread_changed(false);
  // 已添加的记录打开现有好友资料；待处理申请继续复用接受好友流程。
  if (apply->_status != 0) {
    if (auto user = UserMgr::GetInstance()->GetFriendById(apply->_uid)) {
      ShowFriend(user);
      return;
    }
  }
  ui->friend_apply_page->SetApplyInfo(apply);
  ui->contact_detail_stack->setCurrentWidget(ui->friend_apply_page);
}

void ContactsSection::ShowPlaceholder(const QString &title, const QString &description)
{
  ui->placeholder_title->setText(title);
  ui->placeholder_description->setText(description);
  ui->contact_detail_stack->setCurrentWidget(ui->contact_placeholder_page);
}

void ContactsSection::Refresh()
{
  ui->con_user_list->Reload();
  ui->friend_apply_page->Reload();
  bool pending = false;
  for (const auto &apply : UserMgr::GetInstance()->GetApplyList())
    pending |= apply && apply->_status == 0;
  ui->con_user_list->ShowRedPoint(pending);
  emit sig_unread_changed(pending);
  if (!ui->search_edit->text().trimmed().isEmpty() && !_searchTimer.isActive())
    _searchTimer.start(0);
}

void ContactsSection::AddApply(std::shared_ptr<AddFriendApply> apply)
{
  if (!apply)
    return;
  ui->friend_apply_page->AddNewApply(apply);
  ui->con_user_list->Reload();
  ui->con_user_list->ShowRedPoint(true);
  emit sig_unread_changed(true);
}

void ContactsSection::FriendRemoved(int uid)
{
  if (ui->friend_info_page->UserId() != uid)
    return;
  ui->friend_info_page->Reset();
  if (ui->contact_detail_stack->currentWidget() == ui->friend_info_page)
    ShowPlaceholder(tr("联系人"), tr("好友关系已解除，选择其他联系人查看资料"));
}

void ContactsSection::Reset()
{
  _searchTimer.stop();
  ui->search_edit->clear();
  ui->search_list->ClearResults();
  ui->con_user_list->Reload(false);
  ui->friend_apply_page->Reset();
  ui->friend_info_page->Reset();
  ShowPlaceholder(tr("联系人"), tr("选择联系人查看资料"));
  ShowSearch(false);
  emit sig_unread_changed(false);
}

bool ContactsSection::eventFilter(QObject *watched, QEvent *event)
{
  if (event->type() == QEvent::MouseButtonPress && isVisible() && ui->search_list->isVisible()) {
    const auto *widget = qobject_cast<QWidget *>(watched);
    if (widget && isAncestorOf(widget) && widget->window() == window()) {
      const QPoint pos = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
      if (!ui->search_edit->rect().contains(ui->search_edit->mapFromGlobal(pos)) &&
          !ui->search_list->rect().contains(ui->search_list->mapFromGlobal(pos)))
        ui->search_edit->clear();
    }
  }
  return QWidget::eventFilter(watched, event);
}
