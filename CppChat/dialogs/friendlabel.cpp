#include "friendlabel.h"
#include "ui_friendlabel.h"
#include "widgets/clickedlabel.h"
#include <QDebug>
#include <QLayout>

FriendLabel::FriendLabel(QWidget *parent) : QFrame(parent), ui(new Ui::FriendLabel)
{
  ui->setupUi(this);
  ui->close_lb->SetState("normal", "hover", "pressed", "select_normal", "select_hover",
                         "select_pressed");
  connect(ui->close_lb, &ClickedLabel::clicked, this, &FriendLabel::slot_close);
}

FriendLabel::~FriendLabel()
{
  delete ui;
}

void FriendLabel::SetText(QString text)
{
  _text = text;
  ui->tip_lb->setTextFormat(Qt::PlainText);
  ui->tip_lb->setText(_text);
  ensurePolished();
  ui->tip_lb->ensurePolished();
  // 按布局计算完整尺寸，包含文字、关闭区域和内边距。
  layout()->invalidate();
  setFixedSize(layout()->sizeHint());
  _width = this->width();
  _height = this->height();
}

int FriendLabel::Width()
{
  return _width;
}

int FriendLabel::Height()
{
  return _height;
}

QString FriendLabel::Text()
{
  return _text;
}

void FriendLabel::slot_close()
{
  emit sig_close(_text);
}
