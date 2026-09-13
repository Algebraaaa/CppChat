#include "chatuserwid.h"
#include "ui_chatuserwid.h"
#include <QLoggingCategory>
#include "network/usermgr.h"
#include <QLabel>

Q_LOGGING_CATEGORY(chatUserWidgetLog, "cppchat.ui.chat_user_widget")

ChatUserWid::ChatUserWid(QWidget *parent) : ListItemBase(parent), ui(new Ui::ChatUserWid)
{
  ui->setupUi(this);
  SetItemType(ListItemType::CHAT_USER_ITEM);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  ui->time_lb->clear();
  _unreadLabel = new QLabel(this);
  _unreadLabel->setObjectName("unread_dot");
  _unreadLabel->setFixedSize(8, 8);
  _unreadLabel->move(45, 5);
  _unreadLabel->hide();
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
    pixmap.load(":/res/head_1.jpg");
  }

  // 设置图片自动缩放
  ui->icon_lb->setPixmap(
    pixmap.scaled(ui->icon_lb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  ui->icon_lb->setScaledContents(true);

  ui->user_name_lb->setText(_name);
  ui->user_chat_lb->setText(_msg);
}

void ChatUserWid::SetChatData(std::shared_ptr<ChatThreadData> data)
{
  if (!data) return;
  const auto user = UserMgr::GetInstance()->GetFriendById(data->GetOtherId());
  const auto name = user ? (user->_back.isEmpty() ? user->_name : user->_back)
                         : tr("用户 %1").arg(data->GetOtherId());
  const auto icon = user ? user->_icon : QStringLiteral(":/res/head_1.jpg");
  SetInfo(name, icon, data->GetLastMsg());
}
void ChatUserWid::ShowRedPoint(bool show)
{ _unreadLabel->setVisible(show); _unreadLabel->raise(); }
