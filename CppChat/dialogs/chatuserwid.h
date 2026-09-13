#ifndef CHATUSERWID_H
#define CHATUSERWID_H

#include "common/listitembase.h"
#include <QWidget>
#include "common/userdata.h"
class QLabel;
namespace Ui {
class ChatUserWid;
}

class ChatUserWid : public ListItemBase {
  Q_OBJECT

public:
  explicit ChatUserWid(QWidget *parent = nullptr);
  ~ChatUserWid();
  QSize sizeHint() const override;
  void SetInfo(QString name, QString head, QString msg);
  void SetChatData(std::shared_ptr<ChatThreadData> data);
  void ShowRedPoint(bool show);

private:
  Ui::ChatUserWid *ui;
  QLabel *_unreadLabel = nullptr;
  QString _name;
  QString _head;
  QString _msg;
};

#endif // CHATUSERWID_H
