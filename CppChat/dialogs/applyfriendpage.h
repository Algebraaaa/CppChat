#ifndef APPLYFRIENDPAGE_H
#define APPLYFRIENDPAGE_H

#include "applyfrienditem.h"
#include "common/userdata.h"
#include <QJsonArray>
#include <QWidget>
#include <memory>
#include <unordered_map>

namespace Ui {
class ApplyFriendPage;
}

class ApplyFriendPage : public QWidget {
  Q_OBJECT

public:
  explicit ApplyFriendPage(QWidget *parent = nullptr);
  ~ApplyFriendPage();
  void AddNewApply(std::shared_ptr<AddFriendApply> apply);

protected:
  void paintEvent(QPaintEvent *event);

private:
  void loadApplyList();
  Ui::ApplyFriendPage *ui;
  std::unordered_map<int, ApplyFriendItem *> _unauth_items;
public slots:
  void slot_auth_rsp(std::shared_ptr<AuthRsp>);
signals:
  void sig_show_search(bool);
};

#endif // APPLYFRIENDPAGE_H
