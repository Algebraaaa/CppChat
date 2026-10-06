#ifndef CONTACTSSECTION_H
#define CONTACTSSECTION_H

#include "common/userdata.h"
#include <QWidget>
#include <QTimer>
namespace Ui {
class ContactsSection;
}

// 完整通讯录模块：联系人搜索/列表，以及资料、好友申请和占位详情。
class ContactsSection : public QWidget
{
  Q_OBJECT
public:
  explicit ContactsSection(QWidget *parent = nullptr);
  ~ContactsSection() override;
  void Reset();
  void Refresh();
  void AddApply(std::shared_ptr<AddFriendApply> apply);
  void FriendRemoved(int uid);

signals:
  void sig_open_chat(std::shared_ptr<UserInfo> user);
  void sig_unread_changed(bool unread);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void ShowSearch(bool show);
  void ShowFriend(std::shared_ptr<UserInfo> user);
  void ShowNewFriends();
  void ShowApplication(std::shared_ptr<ApplyInfo> apply);
  void ShowPlaceholder(const QString &title, const QString &description);
  Ui::ContactsSection *ui;
  QTimer _searchTimer;
};
#endif
