#include "dialogtitlebar.h"

#include <QEvent>
#include <QMouseEvent>
#include <QWindow>

DialogTitleBar::DialogTitleBar(QWidget *parent) : QWidget(parent)
{
  // 自定义 QWidget 也需要绘制 QSS 定义的背景和圆角。
  setAttribute(Qt::WA_StyledBackground);
}

bool DialogTitleBar::event(QEvent *event)
{
  if (event->type() == QEvent::UngrabMouse)
    _dragging = false;
  return QWidget::event(event);
}

void DialogTitleBar::mousePressEvent(QMouseEvent *event)
{
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }

  auto *dialog = window();
  // 优先使用系统拖动。关闭按钮独立接收点击，不会进入这里。
  if (dialog->windowHandle() && dialog->windowHandle()->startSystemMove()) {
    event->accept();
    return;
  }

  // 系统不支持移动接口时，按全局坐标移动；抓住鼠标后移出标题栏仍能拖动。
  _dragOffset = event->globalPosition().toPoint() - dialog->frameGeometry().topLeft();
  _dragging = true;
  grabMouse();
  event->accept();
}

void DialogTitleBar::mouseMoveEvent(QMouseEvent *event)
{
  if (_dragging && event->buttons().testFlag(Qt::LeftButton)) {
    window()->move(event->globalPosition().toPoint() - _dragOffset);
    event->accept();
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void DialogTitleBar::mouseReleaseEvent(QMouseEvent *event)
{
  if (_dragging && event->button() == Qt::LeftButton) {
    _dragging = false;
    releaseMouse();
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}
