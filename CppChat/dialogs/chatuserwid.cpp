#include "chatuserwid.h"
#include "ui_chatuserwid.h"
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(chatUserWidgetLog, "cppchat.ui.chat_user_widget")

ChatUserWid::ChatUserWid(QWidget *parent) : ListItemBase(parent), ui(new Ui::ChatUserWid)
{
  ui->setupUi(this);
  SetItemType(ListItemType::CHAT_USER_ITEM);
}

ChatUserWid::~ChatUserWid()
{
  delete ui;
}

QSize ChatUserWid::sizeHint() const
{
  return QSize(250, 70); // 返回自定义的尺寸
}

void ChatUserWid::SetInfo(QString name, QString head, QString msg)
{
  _name = name;
  _head = head;
  _msg = msg;
  // 加载图片
  QPixmap pixmap(_head);
  if (pixmap.isNull()) {
    qCWarning(chatUserWidgetLog) << "Failed to load avatar resource"
                                 << "user=" << _name << "resource=" << _head;
  }

  // 设置图片自动缩放
  ui->icon_lb->setPixmap(
    pixmap.scaled(ui->icon_lb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  ui->icon_lb->setScaledContents(true);

  ui->user_name_lb->setText(_name);
  ui->user_chat_lb->setText(_msg);
}
