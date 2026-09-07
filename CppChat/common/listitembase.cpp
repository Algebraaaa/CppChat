#include "listitembase.h"
#include <QPainter>
#include <QStyleOption>
ListItemBase::ListItemBase(QWidget *parent) : QWidget(parent) {}

void ListItemBase::SetItemType(ListItemType itemType)
{
  _itemType = itemType;
}
void ListItemBase::paintEvent(QPaintEvent *)
{
  QStyleOption opt;
  opt.initFrom(this);
  QPainter p(this);
  style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}
ListItemType ListItemBase::GetItemType()
{
  return _itemType;
}
