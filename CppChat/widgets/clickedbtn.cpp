#include "widgets/clickedbtn.h"
#include "common/global.h"
#include <QMouseEvent>

ClickedBtn::ClickedBtn(QWidget *parent) : QPushButton(parent)
{
  // 鼠标移到按钮上时显示手形光标
  setCursor(Qt::PointingHandCursor);
}

ClickedBtn::~ClickedBtn() {}

void ClickedBtn::SetState(const QString &normal, const QString &hover, const QString &press)
{
  _hover = hover;
  _normal = normal;
  _press = press;

  applyState(_normal);
}

void ClickedBtn::enterEvent(QEnterEvent *event)
{
  applyState(_hover);
  QPushButton::enterEvent(event);
}

void ClickedBtn::leaveEvent(QEvent *event)
{
  applyState(_normal);
  QPushButton::leaveEvent(event);
}

void ClickedBtn::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton) {
    applyState(_press);
  }
  QPushButton::mousePressEvent(event);
}

void ClickedBtn::mouseReleaseEvent(QMouseEvent *event)
{
  // 只处理鼠标左键释放
  if (event->button() == Qt::LeftButton) {
    // 三元运算符：鼠标仍在按钮上用 _hover，否则用 _normal
    applyState(underMouse() ? _hover : _normal);
  }
  QPushButton::mouseReleaseEvent(event);
}

// 统一应用动态属性，避免每个鼠标事件重复写相同的刷新代码。
void ClickedBtn::applyState(const QString &state)
{
  setProperty("state", state);
  repolish(this);
  update();
}
