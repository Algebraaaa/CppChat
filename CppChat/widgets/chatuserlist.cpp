#include "chatuserlist.h"

ChatUserList::ChatUserList(QWidget *parent) : SmoothScrollList(parent)
{
}

void ChatUserList::onReachedBottom()
{
  emit sig_loading_chat_user();
}
