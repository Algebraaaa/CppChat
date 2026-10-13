#ifndef CHATUSERLIST_H
#define CHATUSERLIST_H

#include "widgets/smoothscrolllist.h"
class QContextMenuEvent;

class ChatUserList : public SmoothScrollList {
  Q_OBJECT
public:
  explicit ChatUserList(QWidget *parent = nullptr);

protected:
  void onReachedBottom() override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void contextMenuEvent(QContextMenuEvent *event) override;

signals:
  void sig_loading_chat_user();
  void sig_context_menu_requested(QListWidgetItem *item, const QPoint &globalPosition);
};

#endif // CHATUSERLIST_H
