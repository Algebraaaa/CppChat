#include "applyfriendpage.h"
#include "applyfriend.h"
#include "ui_applyfriendpage.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include <QPainter>
#include <QStyleOption>

ApplyFriendPage::ApplyFriendPage(QWidget *parent) : QWidget(parent), ui(new Ui::ApplyFriendPage)
{
  ui->setupUi(this);
  connect(ui->apply_friend_list, &ApplyFriendList::sig_show_search, this, &ApplyFriendPage::sig_show_search);
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_auth_rsp, this, &ApplyFriendPage::slot_auth_rsp);
}
ApplyFriendPage::~ApplyFriendPage() { delete ui; }
void ApplyFriendPage::Reload()
{
  _unauth_items.clear();
  ui->apply_friend_list->clear();
  loadApplyList();
}
void ApplyFriendPage::loadApplyList()
{
  for (const auto &apply : UserMgr::GetInstance()->GetApplyList()) {
    auto *widget = new ApplyFriendItem;
    widget->SetInfo(apply);
    widget->ShowAddBtn(apply->_status == 0);
    auto *item = new QListWidgetItem;
    item->setSizeHint(widget->sizeHint());
    // 不禁用整行，否则行内的“接受”按钮也无法点击。
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    ui->apply_friend_list->insertItem(0, item);
    ui->apply_friend_list->setItemWidget(item, widget);
    if (apply->_status == 0) _unauth_items[apply->_uid] = widget;
    connect(widget, &ApplyFriendItem::sig_auth_friend, this, [this](std::shared_ptr<ApplyInfo> info) {
      auto *dialog = new ApplyFriend(this);
      dialog->setAttribute(Qt::WA_DeleteOnClose);
      dialog->SetApplyInfo(info);
      dialog->open();
    });
  }
}
void ApplyFriendPage::AddNewApply(std::shared_ptr<AddFriendApply>) { Reload(); }
void ApplyFriendPage::slot_auth_rsp(std::shared_ptr<AuthRsp> auth)
{
  const auto item = _unauth_items.find(auth->_uid);
  if (item != _unauth_items.end()) {
    item->second->ShowAddBtn(false);
    _unauth_items.erase(item);
  }
}
void ApplyFriendPage::paintEvent(QPaintEvent *)
{
  QStyleOption option;
  option.initFrom(this);
  QPainter painter(this);
  style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}
