#include "chatview.h"
#include <QCursor>
#include <QEasingCurve>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QScrollBar>
#include <QSet>
#include <QStyleOption>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

namespace {
constexpr int kWheelScrollDistance = 60;
constexpr int kScrollAnimationDuration = 140;
constexpr int kBottomTolerance = 2;
} // namespace

ChatView::ChatView(QWidget *parent) : QWidget(parent)
{
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  m_pScrollArea = new QScrollArea(this);
  m_pScrollArea->setObjectName("chat_area");
  mainLayout->addWidget(m_pScrollArea);

  auto *content = new QWidget;
  content->setObjectName("chat_bg");
  content->setAutoFillBackground(true);
  auto *rows = new QVBoxLayout(content);
  rows->addStretch(100000);
  m_pScrollArea->setWidget(content);
  m_pScrollArea->setWidgetResizable(true);
  m_pScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  // 始终预留原生滚动条宽度，只改变透明度。hide/show 会改变 viewport 宽度，
  // 让长消息重新换行、改变滚动范围，造成鼠标进入/离开时的位置回跳。
  m_pScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  m_pVScrollBar = m_pScrollArea->verticalScrollBar();
  m_pVScrollBar->setObjectName("chat_view_scrollbar");
  m_pVScrollBar->setSingleStep(12);
  _barOpacity = new QGraphicsOpacityEffect(m_pVScrollBar);
  m_pVScrollBar->setGraphicsEffect(_barOpacity);
  _barOpacity->setOpacity(0);
  m_pVScrollBar->setAttribute(Qt::WA_TransparentForMouseEvents);

  m_pScrollAnimation = new QPropertyAnimation(m_pVScrollBar, "value", this);
  m_pScrollAnimation->setDuration(kScrollAnimationDuration);
  m_pScrollAnimation->setEasingCurve(QEasingCurve::OutCubic);
  _layoutTimer = new QTimer(this);
  _layoutTimer->setSingleShot(true);
  connect(_layoutTimer, &QTimer::timeout, this, &ChatView::restoreAnchor);
  connect(m_pVScrollBar, &QScrollBar::rangeChanged, this, &ChatView::onVScrollBarMoved);
  connect(m_pVScrollBar, &QScrollBar::valueChanged, this, [this](int value) {
    // 布局变化时 Qt 可能临时夹紧 value；不要把这种变化当成用户滚动，
    // 否则会覆盖之前记录的消息锚点。
    if (_restoringScroll || _batchUpdating || _layoutTimer->isActive())
      return;
    if (m_pScrollAnimation->state() != QAbstractAnimation::Running) {
      m_scrollTarget = value;
      _followBottom = value >= m_pVScrollBar->maximum() - kBottomTolerance;
    }
    captureAnchor();
  });
  connect(m_pScrollAnimation, &QPropertyAnimation::finished, this, [this] {
    _followBottom = m_pVScrollBar->value() >= m_pVScrollBar->maximum() - kBottomTolerance;
    captureAnchor();
  });
  connect(m_pVScrollBar, &QScrollBar::sliderPressed, this, [this] {
    _layoutTimer->stop();
    m_pScrollAnimation->stop();
    _followBottom = false;
    m_scrollTarget = m_pVScrollBar->value();
    captureAnchor();
  });
  connect(m_pVScrollBar, &QScrollBar::sliderReleased, this, [this] {
    _followBottom = m_pVScrollBar->value() >= m_pVScrollBar->maximum() - kBottomTolerance;
    captureAnchor();
    updateBarVisibility();
  });
  m_pScrollArea->viewport()->installEventFilter(this);
  m_pVScrollBar->installEventFilter(this);
}

void ChatView::captureAnchor()
{
  if (_restoringScroll || _batchUpdating || _layoutTimer->isActive())
    return;
  _savedValue = m_pVScrollBar->value();
  _anchor.clear();
  _anchorKey.clear();
  auto *rows = qobject_cast<QVBoxLayout *>(m_pScrollArea->widget()->layout());
  auto *viewport = m_pScrollArea->viewport();
  // 保存最上方可见消息和它距 viewport 顶部的像素距离，而不是只保存 value。
  for (int index = 0; index < rows->count() - 1; ++index) {
    auto *item = rows->itemAt(index)->widget();
    if (!item)
      continue;
    const int top = item->mapTo(viewport, QPoint(0, 0)).y();
    if (top + item->height() > 0 && top < viewport->height()) {
      _anchor = item;
      _anchorKey = item->property("chat_scroll_key").toString();
      _anchorOffset = top;
      break;
    }
  }
}

void ChatView::restoreAnchor()
{
  if (_batchUpdating)
    return;
  _restoringScroll = true;
  auto *rows = qobject_cast<QVBoxLayout *>(m_pScrollArea->widget()->layout());
  rows->activate();
  // 整页刷新时原控件可能已经销毁，用消息标识找回同一条消息。
  if (!_anchor && !_anchorKey.isEmpty()) {
    for (int index = 0; index < rows->count() - 1; ++index) {
      auto *item = rows->itemAt(index)->widget();
      if (item && item->property("chat_scroll_key").toString() == _anchorKey) {
        _anchor = item;
        break;
      }
    }
  }
  const int current = m_pVScrollBar->value();
  int target = _savedValue;
  if (_followBottom)
    target = m_pVScrollBar->maximum();
  else if (_anchor)
    target = current + _anchor->mapTo(m_pScrollArea->viewport(), QPoint(0, 0)).y() - _anchorOffset;
  target = qBound(m_pVScrollBar->minimum(), target, m_pVScrollBar->maximum());
  const int correction = target - current;
  const bool running = m_pScrollAnimation->state() == QAbstractAnimation::Running;
  const int animationTarget = m_scrollTarget + correction;
  if (correction != 0 || _followBottom) {
    m_pScrollAnimation->stop();
    m_pVScrollBar->setValue(target);
    m_scrollTarget = target;
    if (running && !_followBottom) {
      m_scrollTarget = qBound(m_pVScrollBar->minimum(), animationTarget, m_pVScrollBar->maximum());
      m_pScrollAnimation->setStartValue(target);
      m_pScrollAnimation->setEndValue(m_scrollTarget);
      m_pScrollAnimation->start();
    }
  }
  _restoringScroll = false;
  captureAnchor();
  updateBarVisibility();
}

void ChatView::beginUpdate(bool scrollToBottom)
{
  if (_layoutTimer->isActive()) {
    _layoutTimer->stop();
    restoreAnchor();
  }
  captureAnchor();
  m_pScrollAnimation->stop();
  _batchUpdating = true;
  if (scrollToBottom)
    _followBottom = true;
  setUpdatesEnabled(false);
}

void ChatView::endUpdate()
{
  _batchUpdating = false;
  _layoutTimer->start(0);
  restoreAnchor(); // 先修正一次位置，再允许绘制，避免刷新后闪现旧的滚动位置。
  setUpdatesEnabled(true);
}

void ChatView::scrollToBottom()
{
  _followBottom = true;
  m_pScrollAnimation->stop();
  _layoutTimer->start(0);
}

void ChatView::appendChatItem(QWidget *item)
{
  captureAnchor();
  auto *rows = qobject_cast<QVBoxLayout *>(m_pScrollArea->widget()->layout());
  rows->insertWidget(rows->count() - 1, item);
  if (!_batchUpdating)
    _layoutTimer->start(0);
}

void ChatView::prependChatItem(QWidget *item)
{
  captureAnchor();
  auto *rows = qobject_cast<QVBoxLayout *>(m_pScrollArea->widget()->layout());
  rows->insertWidget(0, item);
  if (!_batchUpdating)
    _layoutTimer->start(0);
}

void ChatView::insertChatItem(QWidget *before, QWidget *item)
{
  captureAnchor();
  auto *rows = qobject_cast<QVBoxLayout *>(m_pScrollArea->widget()->layout());
  const int index = rows->indexOf(before);
  rows->insertWidget(index < 0 ? rows->count() - 1 : index, item);
  if (!_batchUpdating)
    _layoutTimer->start(0);
}

void ChatView::setChatItems(const QList<QWidget *> &items)
{
  auto *rows = qobject_cast<QVBoxLayout *>(m_pScrollArea->widget()->layout());
  const QSet<QWidget *> wanted(items.cbegin(), items.cend());
  // 保留已有消息控件和滚动锚点，只删除退出当前窗口的行。
  for (int index = rows->count() - 2; index >= 0; --index) {
    auto *widget = rows->itemAt(index)->widget();
    if (!wanted.contains(widget)) {
      auto *entry = rows->takeAt(index);
      delete widget;
      delete entry;
    }
  }
  for (int index = 0; index < items.size(); ++index) {
    auto *widget = items[index];
    if (rows->indexOf(widget) != index)
      rows->insertWidget(index, widget);
    widget->show();
  }
}

void ChatView::removeAllItem()
{
  m_pScrollAnimation->stop();
  if (!_batchUpdating) {
    _layoutTimer->stop();
    _followBottom = true;
    _savedValue = m_scrollTarget = 0;
    _anchor.clear();
    _anchorKey.clear();
  }
  auto *rows = qobject_cast<QVBoxLayout *>(m_pScrollArea->widget()->layout());
  while (rows->count() > 1) {
    auto *item = rows->takeAt(0);
    delete item->widget();
    delete item;
  }
}

bool ChatView::eventFilter(QObject *object, QEvent *event)
{
  if (object == m_pVScrollBar &&
      (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::KeyPress)) {
    m_pScrollAnimation->stop();
    _followBottom = false;
    m_scrollTarget = m_pVScrollBar->value();
  }
  if ((object == m_pScrollArea->viewport() || object == m_pVScrollBar) &&
      event->type() == QEvent::Wheel && handleWheelEvent(static_cast<QWheelEvent *>(event)))
    return true;
  return QWidget::eventFilter(object, event);
}

bool ChatView::handleWheelEvent(QWheelEvent *event)
{
  if (_layoutTimer->isActive()) {
    _layoutTimer->stop();
    restoreAnchor();
  }
  if (!event->pixelDelta().isNull()) {
    m_pScrollAnimation->stop();
    _followBottom = false;
    captureAnchor();
    return false; // 触控板连续像素事件交给 QScrollArea。
  }
  const int delta = event->angleDelta().y();
  if (delta == 0 || m_pVScrollBar->maximum() <= m_pVScrollBar->minimum())
    return false;
  const int distance = qRound(qreal(delta) / 120 * kWheelScrollDistance);
  const int current = m_pVScrollBar->value();
  // 反向滚动立即以当前位置为起点，不再先完成上一个方向积攒的动画距离。
  if (m_pScrollAnimation->state() != QAbstractAnimation::Running ||
      (distance > 0 && m_scrollTarget > current) || (distance < 0 && m_scrollTarget < current))
    m_scrollTarget = current;
  m_scrollTarget =
      qBound(m_pVScrollBar->minimum(), m_scrollTarget - distance, m_pVScrollBar->maximum());
  _followBottom = false;
  captureAnchor();
  m_pScrollAnimation->stop();
  m_pScrollAnimation->setStartValue(current);
  m_pScrollAnimation->setEndValue(m_scrollTarget);
  m_pScrollAnimation->start();
  event->accept();
  return true;
}

void ChatView::onVScrollBarMoved(int minimum, int maximum)
{
  m_scrollTarget = qBound(minimum, m_scrollTarget, maximum);
  // 删除旧的“插入后 500ms 内每次 rangeChanged 都强制到底部”规则。
  // 只有原本就在底部才跟随；阅读历史时通过消息锚点修正高度变化。
  if (!_batchUpdating)
    _layoutTimer->start(0);
  updateBarVisibility();
}

void ChatView::updateBarVisibility()
{
  const bool hasContent = m_pVScrollBar->maximum() > m_pVScrollBar->minimum();
  const bool inside = rect().contains(mapFromGlobal(QCursor::pos()));
  const bool visible = hasContent && (inside || m_pVScrollBar->isSliderDown());
  _barOpacity->setOpacity(visible ? 1 : 0);
  m_pVScrollBar->setAttribute(Qt::WA_TransparentForMouseEvents, !visible);
}

void ChatView::enterEvent(QEnterEvent *event)
{
  QWidget::enterEvent(event);
  updateBarVisibility();
}

void ChatView::leaveEvent(QEvent *event)
{
  QWidget::leaveEvent(event);
  updateBarVisibility();
}

void ChatView::paintEvent(QPaintEvent *)
{
  QStyleOption option;
  option.initFrom(this);
  QPainter painter(this);
  style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}
