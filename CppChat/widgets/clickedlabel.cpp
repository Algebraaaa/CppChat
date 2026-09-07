#include "widgets/clickedlabel.h"
#include <QEnterEvent>
#include <QMouseEvent>

ClickedLabel::ClickedLabel(QWidget *parent) : QLabel(parent)
{
  // 使用手形光标
  setCursor(Qt::PointingHandCursor);
}

// 处理鼠标按下事件。
void ClickedLabel::mousePressEvent(QMouseEvent *event)
{
  // 只响应鼠标左键
  if (event->button() == Qt::LeftButton) {
    // 记住左键是从当前控件开始按下的
    _left_button_pressed = true;
    // 如果配置了 press 就显示按下样式；否则沿用 hover 样式
    applyState(_press.isEmpty() ? _hover : _press);
    // 通知外部：鼠标左键已经按下。
    emit pressed();
  }
  QLabel::mousePressEvent(event);
}

void ClickedLabel::mouseReleaseEvent(QMouseEvent *event)
{
  // 只响应鼠标左键释放
  if (event->button() == Qt::LeftButton) {
    // 必须从本控件按下并在本控件内释放，才算完成一次有效点击
    const bool shouldEmitClicked = _left_button_pressed && underMouse();
    // 本次按压过程已经结束
    _left_button_pressed = false;
    // 鼠标仍在控件上恢复 hover；已经移出则恢复 normal
    applyState(underMouse() ? _hover : _normal);
    // 通知外部鼠标左键已经释放
    emit released();

    // clicked 放在释放阶段，更符合 QPushButton 等 Qt 控件的标准点击语义
    if (shouldEmitClicked) {
      emit clicked();
    }
  }

  QLabel::mouseReleaseEvent(event);
}

void ClickedLabel::enterEvent(QEnterEvent *event)
{
  // 左键仍处于按下状态时显示 press，否则显示普通 hover。
  applyState(_left_button_pressed && !_press.isEmpty() ? _press : _hover);
  // 把事件继续交给 QLabel 基类处理。
  QLabel::enterEvent(event);
}

void ClickedLabel::leaveEvent(QEvent *event)
{
  // 鼠标离开后恢复 normal；如果随后在外部释放，也不会发出 clicked。
  applyState(_normal);
  // 调用 QLabel 的默认离开事件处理。
  QLabel::leaveEvent(event);
}

void ClickedLabel::SetState(const QString &normal, const QString &hover, const QString &press)
{

  _normal = normal;
  _hover = hover;
  _press = press;
  applyState(_normal);
}

// 统一应用动态属性，避免每个鼠标事件重复写相同的刷新代码
void ClickedLabel::applyState(const QString &state)
{
  setProperty("state", state);
  repolish(this);
  update();
}
