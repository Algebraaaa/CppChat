#include "applyfriendlist.h"
#include <QEvent>
#include <QMouseEvent>
ApplyFriendList::ApplyFriendList(QWidget *parent) : SmoothScrollList(parent)
{
  // 保留点击列表时收起搜索层的业务行为。
  this->viewport()->installEventFilter(this);
}

bool ApplyFriendList::eventFilter(QObject *watched, QEvent *event)
{

  if (watched == this->viewport()) {
    if (event->type() == QEvent::MouseButtonPress) {
      emit sig_show_search(false);
    }
  }
  return SmoothScrollList::eventFilter(watched, event);
}
