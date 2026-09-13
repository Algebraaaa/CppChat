#ifndef APPLYFRIENDLIST_H
#define APPLYFRIENDLIST_H
#include "widgets/smoothscrolllist.h"
class ApplyFriendList : public SmoothScrollList {
  Q_OBJECT
public:
  ApplyFriendList(QWidget *parent = nullptr);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private slots:

signals:
  void sig_show_search(bool);
};

#endif // APPLYFRIENDLIST_H
