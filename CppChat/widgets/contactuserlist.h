#ifndef CONTACTUSERLIST_H
#define CONTACTUSERLIST_H
#include "common/userdata.h"
#include "smoothscrolllist.h"
#include <QColor>
class ContactUserList : public SmoothScrollList {
  Q_OBJECT
  Q_PROPERTY(QColor secondaryColor MEMBER _secondaryColor)
  Q_PROPERTY(QColor dividerColor MEMBER _dividerColor)
  Q_PROPERTY(QColor applicationSelectionColor MEMBER _applicationSelectionColor)
public:
  explicit ContactUserList(QWidget *parent = nullptr);
  void Reload(bool preserveSelection = true);
  void ShowRedPoint(bool show = true);
  void SelectNewFriends();
  bool ActivateCurrent();

signals:
  void sig_switch_apply_friend_page();
  void sig_switch_friend_info_page(std::shared_ptr<UserInfo> user);
  void sig_open_friend_apply(std::shared_ptr<ApplyInfo> apply);
  void sig_show_placeholder(const QString &title, const QString &description);

private:
  void slot_item_clicked(QListWidgetItem *item);
  void SetContactsExpanded(bool expanded);
  void SetApplicationsExpanded(bool expanded);
  QListWidgetItem *_newFriends = nullptr;
  QListWidgetItem *_contactsHeader = nullptr;
  QVector<QListWidgetItem *> _contactRows;
  QVector<QListWidgetItem *> _applicationRows;
  bool _contactsExpanded = true;
  bool _applicationsExpanded = true;
  bool _redPoint = false;
  QColor _secondaryColor = QColor("#858b93");
  QColor _dividerColor = QColor("#e3e3e7");
  QColor _applicationSelectionColor = QColor("#e1e1e5");
};
#endif
