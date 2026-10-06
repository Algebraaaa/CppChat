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
  connect(ui->apply_friend_list, &ApplyFriendList::sig_show_search, this,
          &ApplyFriendPage::sig_show_search);
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_auth_rsp, this,
          &ApplyFriendPage::slot_auth_rsp);
}
ApplyFriendPage::~ApplyFriendPage()
{
  delete ui;
}
void ApplyFriendPage::Reload()
{
  _unauth_items.clear();
  ui->apply_friend_list->clear();
  loadSelectedApply();
}
void ApplyFriendPage::SetApplyInfo(std::shared_ptr<ApplyInfo> apply)
{
  _selectedUid = apply ? apply->_uid : 0;
  Reload();
}
void ApplyFriendPage::Reset()
{
  _selectedUid = 0;
  Reload();
}
void ApplyFriendPage::loadSelectedApply()
{
  // 完整申请列表在左侧“新的朋友”分组，右侧只呈现选中的一条申请。
  for (const auto &apply : UserMgr::GetInstance()->GetApplyList()) {
    if (!apply || apply->_uid != _selectedUid)
      continue;
    auto *widget = new ApplyFriendItem;
    widget->SetInfo(apply);
    widget->ShowAddBtn(apply->_status == 0);
    auto *item = new QListWidgetItem;
    item->setSizeHint(widget->sizeHint());
    // 不禁用整行，否则行内的“接受”按钮也无法点击。
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    ui->apply_friend_list->insertItem(0, item);
    ui->apply_friend_list->setItemWidget(item, widget);
    if (apply->_status == 0)
      _unauth_items[apply->_uid] = widget;
    connect(widget, &ApplyFriendItem::sig_auth_friend, this,
            [this](std::shared_ptr<ApplyInfo> info) {
              auto *dialog = new ApplyFriend(this);
              dialog->setAttribute(Qt::WA_DeleteOnClose);
              dialog->SetApplyInfo(info);
              dialog->open();
            });
  }
  ui->apply_hint->setVisible(ui->apply_friend_list->count() == 0);
}
void ApplyFriendPage::AddNewApply(std::shared_ptr<AddFriendApply>)
{
  Reload();
}
void ApplyFriendPage::slot_auth_rsp(std::shared_ptr<AuthRsp> auth)
{
  if (!auth)
    return;
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
