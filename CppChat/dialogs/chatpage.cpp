#include "chatpage.h"
#include "ui_chatpage.h"
#include "widgets/PictureBubble.h"
#include "widgets/TextBubble.h"
#include "widgets/chatitembase.h"
#include <QPainter>
#include <QStyleOption>
ChatPage::ChatPage(QWidget *parent) : QWidget(parent), ui(new Ui::ChatPage)
{
  ui->setupUi(this);
  // 设置按钮样式
  ui->receive_btn->SetState("normal", "hover", "press");
  ui->send_btn->SetState("normal", "hover", "press");

  // 设置图标样式
  // ui->emo_lb->setCursor(Qt::PointingHandCursor);
  ui->emo_lb->SetState("normal", "hover", "press");
  ui->file_lb->SetState("normal", "hover", "press");
  // ui->file_lb->setCursor(Qt::PointingHandCursor);

  // MessageTextEdit 会在用户按下 Enter 时发出 send()；复用发送按钮的槽函数，
  // 使点击“发送”和按下 Enter 执行完全相同的发送流程。
  connect(ui->chatEdit, &MessageTextEdit::send, this,
          &ChatPage::on_send_btn_clicked);
}

ChatPage::~ChatPage()
{
  delete ui;
}

void ChatPage::paintEvent(QPaintEvent *)
{
  QStyleOption opt;
  opt.initFrom(this);
  QPainter p(this);
  style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void ChatPage::on_send_btn_clicked()
{
  auto *pTextEdit = ui->chatEdit;
  ChatRole role = ChatRole::Self;
  QString userName = QStringLiteral("恋恋风辰");
  QString userIcon = ":/res/head_1.jpg";

  const auto &msgList = pTextEdit->getMsgList();
  for (int i = 0; i < msgList.size(); ++i) {
    QString type = msgList[i].msgFlag;
    ChatItemBase *pChatItem = new ChatItemBase(role);
    pChatItem->setUserName(userName);
    pChatItem->setUserIcon(QPixmap(userIcon));
    QWidget *pBubble = nullptr;
    if (type == "text") {
      pBubble = new TextBubble(role, msgList[i].content);
    } else if (type == "image") {
      pBubble = new PictureBubble(QPixmap(msgList[i].content), role);
    } else if (type == "file") {
    }
    if (pBubble != nullptr) {
      pChatItem->setWidget(pBubble);
      ui->chat_data_list->appendChatItem(pChatItem);
    }
  }
}
