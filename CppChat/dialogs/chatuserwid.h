#ifndef CHATUSERWID_H
#define CHATUSERWID_H

#include "common/listitembase.h"
#include <QWidget>
#include "common/userdata.h"
class QEvent;
namespace Ui {
class ChatUserWid;
}

class ChatUserWid : public ListItemBase
{
  Q_OBJECT

public:
  explicit ChatUserWid(QWidget *parent = nullptr);
  ~ChatUserWid();
  QSize sizeHint() const override;
  void SetInfo(QString name, QString head, QString msg);
  void SetChatData(std::shared_ptr<ChatThreadData> data);
  void SetSearchInfo(const std::shared_ptr<UserInfo> &user, const QString &preview);
  void ShowRedPoint(bool show);
  void SetSelected(bool selected);
  void SetPinned(bool pinned);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void UpdateVisibleText();
  Ui::ChatUserWid *ui;
  QString _name;
  QString _head;
  QString _msg;
  bool _selected = false;
};

#endif // CHATUSERWID_H
