#ifndef CHATUSERLIST_H
#define CHATUSERLIST_H

#include "widgets/smoothscrolllist.h"

class ChatUserList : public SmoothScrollList {
  Q_OBJECT
public:
  explicit ChatUserList(QWidget *parent = nullptr);

protected:
  void onReachedBottom() override;

signals:
  void sig_loading_chat_user();
};

#endif // CHATUSERLIST_H
