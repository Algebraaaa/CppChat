#ifndef CONTACTUSERLIST_H
#define CONTACTUSERLIST_H
#include "smoothscrolllist.h"
#include "common/userdata.h"
class ConUserItem;
class ContactUserList : public SmoothScrollList {
  Q_OBJECT
public:
  explicit ContactUserList(QWidget *parent = nullptr);
  void Reload();
  void LoadMore();
  void ShowRedPoint(bool show = true);
protected:
  void onReachedBottom() override;
signals:
  void sig_loading_contact_user();
  void sig_switch_apply_friend_page();
  void sig_switch_friend_info_page(std::shared_ptr<UserInfo> user);
private:
  void slot_item_clicked(QListWidgetItem *item);
  ConUserItem *_add_friend_item = nullptr;
  int _loadedCount = 0;
  bool _loading = false;
};
#endif
