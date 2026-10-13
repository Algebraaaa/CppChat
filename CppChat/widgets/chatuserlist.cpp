#include "chatuserlist.h"
#include <QContextMenuEvent>
#include <QMouseEvent>

ChatUserList::ChatUserList(QWidget *parent) : SmoothScrollList(parent)
{
  setResizeMode(QListView::Adjust);
}

void ChatUserList::onReachedBottom() { emit sig_loading_chat_user(); }

void ChatUserList::mousePressEvent(QMouseEvent *event)
{
  // 右键只操作菜单目标，不触发会话切换或自动已读。
  if (event->button() == Qt::RightButton) {
    event->accept();
    return;
  }
  SmoothScrollList::mousePressEvent(event);
}

void ChatUserList::contextMenuEvent(QContextMenuEvent *event)
{
  auto *target = event->reason() == QContextMenuEvent::Keyboard
                     ? currentItem()
                     : itemAt(viewport()->mapFromGlobal(event->globalPos()));
  if (target) {
    const QPoint position = event->reason() == QContextMenuEvent::Keyboard
                                ? viewport()->mapToGlobal(visualItemRect(target).center())
                                : event->globalPos();
    emit sig_context_menu_requested(target, position);
  }
  event->accept();
}

void ChatUserList::mouseReleaseEvent(QMouseEvent *event)
{
  if (event->button() == Qt::RightButton) {
    event->accept();
    return;
  }
  SmoothScrollList::mouseReleaseEvent(event);
}
