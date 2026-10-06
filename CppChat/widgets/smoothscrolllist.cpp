#include "smoothscrolllist.h"

#include <QCursor>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QWheelEvent>
#include <QtMath>

namespace {
constexpr int kOverlayScrollBarWidth = 8;
// 分隔条附近先保留拉伸热区；滚动条向列表内侧退让。
constexpr int kOverlayScrollBarRightMargin = 5;
constexpr int kResizeEdgeRevealMargin = 4;
constexpr int kOverlayScrollBarVerticalMargin = 3;
constexpr int kWheelScrollDistance = 60;
constexpr int kScrollAnimationDuration = 140;
} // namespace

SmoothScrollList::SmoothScrollList(QWidget *parent) : QListWidget(parent)
{
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  setMouseTracking(true);
  viewport()->setMouseTracking(true);

  QScrollBar *nativeScrollBar = verticalScrollBar();
  nativeScrollBar->setSingleStep(12);

  _scrollAnimation = new QPropertyAnimation(nativeScrollBar, "value", this);
  _scrollAnimation->setDuration(kScrollAnimationDuration);
  _scrollAnimation->setEasingCurve(QEasingCurve::OutCubic);
  _scrollTarget = nativeScrollBar->value();

  // 原生滚动条负责实际滚动；这个滚动条只覆盖在列表右侧供用户查看和拖动。
  _overlayScrollBar = new QScrollBar(Qt::Vertical, this);
  _overlayScrollBar->setObjectName("smooth_scrollbar");
  _overlayScrollBar->setRange(nativeScrollBar->minimum(), nativeScrollBar->maximum());
  _overlayScrollBar->setPageStep(nativeScrollBar->pageStep());
  _overlayScrollBar->setSingleStep(nativeScrollBar->singleStep());
  _overlayScrollBar->hide();

  connect(nativeScrollBar, &QScrollBar::rangeChanged, this,
          [this, nativeScrollBar](int minimum, int maximum) {
            _overlayScrollBar->setRange(minimum, maximum);
            _overlayScrollBar->setPageStep(nativeScrollBar->pageStep());
            _overlayScrollBar->setSingleStep(nativeScrollBar->singleStep());
            _scrollTarget = qBound(minimum, _scrollTarget, maximum);
            updateScrollBarVisibility();
          });

  connect(nativeScrollBar, &QScrollBar::valueChanged, this, [this, nativeScrollBar](int value) {
    {
      // 同步显示位置时不再反向写回原生滚动条；拖动时的信号仍正常工作。
      const QSignalBlocker blocker(_overlayScrollBar);
      _overlayScrollBar->setValue(value);
    }

    if (_scrollAnimation->state() != QAbstractAnimation::Running) {
      _scrollTarget = value;
    }

    if (nativeScrollBar->maximum() > nativeScrollBar->minimum() &&
        value >= nativeScrollBar->maximum()) {
      onReachedBottom();
    }
  });

  connect(_overlayScrollBar, &QScrollBar::valueChanged, nativeScrollBar, &QScrollBar::setValue);

  connect(_overlayScrollBar, &QScrollBar::sliderPressed, this, [this, nativeScrollBar]() {
    _scrollAnimation->stop();
    _scrollTarget = nativeScrollBar->value();
  });

  connect(_overlayScrollBar, &QScrollBar::sliderReleased, this,
          &SmoothScrollList::updateScrollBarVisibility);

  updateScrollBarGeometry();
}

void SmoothScrollList::enterEvent(QEnterEvent *event)
{
  QListWidget::enterEvent(event);
  updateScrollBarVisibility();
}

void SmoothScrollList::leaveEvent(QEvent *event)
{
  QListWidget::leaveEvent(event);
  updateScrollBarVisibility();
}

void SmoothScrollList::mouseMoveEvent(QMouseEvent *event)
{
  QListWidget::mouseMoveEvent(event);
  updateScrollBarVisibility();
}

void SmoothScrollList::resizeEvent(QResizeEvent *event)
{
  QListWidget::resizeEvent(event);

  QScrollBar *nativeScrollBar = verticalScrollBar();
  _overlayScrollBar->setPageStep(nativeScrollBar->pageStep());
  _overlayScrollBar->setSingleStep(nativeScrollBar->singleStep());
  updateScrollBarGeometry();
  updateScrollBarVisibility();
}

void SmoothScrollList::wheelEvent(QWheelEvent *event)
{
  QScrollBar *nativeScrollBar = verticalScrollBar();

  // 触控板已经提供连续的像素位移，直接交给 QListWidget 处理。
  if (!event->pixelDelta().isNull()) {
    _scrollAnimation->stop();
    QListWidget::wheelEvent(event);
    _scrollTarget = nativeScrollBar->value();
    return;
  }

  const int angleDeltaY = event->angleDelta().y();
  if (angleDeltaY == 0) {
    QListWidget::wheelEvent(event);
    return;
  }

  if (nativeScrollBar->maximum() <= nativeScrollBar->minimum()) {
    event->ignore();
    return;
  }

  const qreal wheelSteps = static_cast<qreal>(angleDeltaY) / 120.0;
  const int scrollDistance = qRound(wheelSteps * kWheelScrollDistance);
  const int current = nativeScrollBar->value();
  // 反向滚动从当前位置起步，避免先把上一方向积攒的距离滚完。
  if (_scrollAnimation->state() != QAbstractAnimation::Running ||
      (scrollDistance > 0 && _scrollTarget > current) ||
      (scrollDistance < 0 && _scrollTarget < current))
    _scrollTarget = current;
  _scrollTarget = qBound(nativeScrollBar->minimum(), _scrollTarget - scrollDistance,
                         nativeScrollBar->maximum());

  _scrollAnimation->stop();
  _scrollAnimation->setStartValue(nativeScrollBar->value());
  _scrollAnimation->setEndValue(_scrollTarget);
  _scrollAnimation->start();
  event->accept();
}

void SmoothScrollList::onReachedBottom() {}

void SmoothScrollList::updateScrollBarGeometry()
{
  const QPoint viewportTopLeft = viewport()->mapTo(this, QPoint(0, 0));
  const QRect viewportRect(viewportTopLeft, viewport()->size());
  const int scrollBarHeight = qMax(0, viewportRect.height() - kOverlayScrollBarVerticalMargin * 2);
  const int scrollBarX = qMax(viewportRect.left(), viewportRect.right() - kOverlayScrollBarWidth -
                                                       kOverlayScrollBarRightMargin + 1);
  const int scrollBarY = viewportRect.top() + kOverlayScrollBarVerticalMargin;

  _overlayScrollBar->setGeometry(scrollBarX, scrollBarY, kOverlayScrollBarWidth, scrollBarHeight);
  _overlayScrollBar->raise();
}

void SmoothScrollList::updateScrollBarVisibility()
{
  const QScrollBar *nativeScrollBar = verticalScrollBar();
  const bool hasScrollableContent = nativeScrollBar->maximum() > nativeScrollBar->minimum();
  const QPoint cursorPosition = mapFromGlobal(QCursor::pos());
  const bool cursorInsideList = rect().contains(cursorPosition);
  const bool cursorPastResizeEdge = cursorPosition.x() <= rect().right() - kResizeEdgeRevealMargin;
  const bool shouldBeVisible =
      (cursorInsideList && cursorPastResizeEdge) || _overlayScrollBar->isSliderDown();

  const bool visible = hasScrollableContent && shouldBeVisible;
  if (visible == !_overlayScrollBar->isHidden())
    return;
  _overlayScrollBar->setVisible(visible);
  if (visible) {
    _overlayScrollBar->raise();
  }
}
