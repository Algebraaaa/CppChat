#include "searchlist.h"
#include "dialogs/adduseritem.h"
#include "dialogs/findsuccessdlg.h"
#include "network/tcpmgr.h"
#include "widgets/customizeedit.h"
#include <QJsonDocument>
#include <QJsonObject>
SearchList::SearchList(QWidget *parent)
  : QListWidget(parent), _find_dlg(nullptr), _search_edit(nullptr), _send_pending(false)
{
  Q_UNUSED(parent);
  this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  // 安装事件过滤器
  this->viewport()->installEventFilter(this);
  // 连接点击的信号和槽
  connect(this, &QListWidget::itemClicked, this, &SearchList::slot_item_clicked);
  // 添加条目
  addTipItem();
  // 连接搜索条目
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_user_search, this,
          &SearchList::slot_user_search);
}
void SearchList::addTipItem()
{
  auto *invalid_item = new QWidget();
  invalid_item->setObjectName("invalid_item");
  QListWidgetItem *item_tmp = new QListWidgetItem;
  // qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
  item_tmp->setSizeHint(QSize(250, 10));
  this->addItem(item_tmp);
  this->setItemWidget(item_tmp, invalid_item);
  item_tmp->setFlags(item_tmp->flags() & ~Qt::ItemIsSelectable);

  auto *add_user_item = new AddUserItem();
  QListWidgetItem *item = new QListWidgetItem;
  // qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
  item->setSizeHint(add_user_item->sizeHint());
  this->addItem(item);
  this->setItemWidget(item, add_user_item);
}

void SearchList::slot_item_clicked(QListWidgetItem *item)
{
  QWidget *widget = this->itemWidget(item); // 获取自定义widget对象
  if (!widget) {
    qDebug() << "slot item clicked widget is nullptr";
    return;
  }

  // 对自定义widget进行操作， 将item 转化为基类ListItemBase
  ListItemBase *customItem = qobject_cast<ListItemBase *>(widget);
  if (!customItem) {
    qDebug() << "slot item clicked widget is nullptr";
    return;
  }

  auto itemType = customItem->GetItemType();
  if (itemType == ListItemType::INVALID_ITEM) {
    qDebug() << "slot invalid item clicked ";
    return;
  }

  if (itemType == ListItemType::ADD_USER_TIP_ITEM) {

    // todo ...
    _find_dlg = std::make_shared<FindSuccessDlg>(this);
    auto si = std::make_shared<SearchInfo>(0, "llfc", "llfc", "hello , my friend!", 0);
    (std::dynamic_pointer_cast<FindSuccessDlg>(_find_dlg))->SetSearchInfo(si);
    _find_dlg->show();
    return;
  }

  // 清楚弹出框
  CloseFindDlg();
}

void SearchList::slot_user_search(std::shared_ptr<SearchInfo> si) {}
void SearchList::CloseFindDlg()
{
  if (_find_dlg) {
    _find_dlg->hide();
    _find_dlg = nullptr;
  }
}
