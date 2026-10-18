#ifndef FRIENDINFOPAGE_H
#define FRIENDINFOPAGE_H

#include "common/userdata.h"
#include <QPointer>
#include <QWidget>

namespace Ui {
class FriendInfoPage;
}
class QAction;
class CommonGroupsDialog;

class FriendInfoPage : public QWidget {
  Q_OBJECT

public:
  explicit FriendInfoPage(QWidget *parent = nullptr);
  ~FriendInfoPage();
  void SetInfo(std::shared_ptr<UserInfo> ui);
  void Reset();
  int UserId() const { return _user_info ? _user_info->_uid : 0; }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
private slots:
  void on_msg_chat_clicked();
  void DeleteFriend();

private:
  Ui::FriendInfoPage *ui;
  std::shared_ptr<UserInfo> _user_info;
  int _deletingUid = 0;
  QAction *_deleteFriendAction = nullptr;
  QAction *_starAction = nullptr;
  QPointer<CommonGroupsDialog> _commonGroups;
  void UpdateActions();
signals:
  void sig_jump_chat_item(std::shared_ptr<UserInfo> si);
  void sig_call_friend(int uid, bool video);
  void sig_open_group(int threadId);
  void sig_show_placeholder(const QString &title, const QString &description);
};

#endif // FRIENDINFOPAGE_H
