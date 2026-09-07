#include "chatview.h"
#include <QCursor>
#include <QEasingCurve>
#include <QEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyleOption>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

namespace {
// 与 ChatUserList 保持相同的鼠标滚轮距离和缓动时间。
constexpr int kWheelScrollDistance = 60;
constexpr int kScrollAnimationDuration = 400;
}

ChatView::ChatView(QWidget *parent)
  : QWidget(parent), m_pScrollArea(nullptr), m_pScrollAnimation(nullptr),
    m_scrollTarget(0), isAppended(false)
{
  QVBoxLayout *pMainLayout = new QVBoxLayout();
  this->setLayout(pMainLayout);
  pMainLayout->setContentsMargins(0, 0, 0, 0);

  m_pScrollArea = new QScrollArea();
  m_pScrollArea->setObjectName("chat_area");
  pMainLayout->addWidget(m_pScrollArea);

  QWidget *w = new QWidget(this);
  w->setObjectName("chat_bg");
  w->setAutoFillBackground(true);

  QVBoxLayout *pVLayout_1 = new QVBoxLayout();
  pVLayout_1->addWidget(new QWidget(), 100000);
  w->setLayout(pVLayout_1);
  m_pScrollArea->setWidget(w);

  m_pScrollArea->setWidgetResizable(true);
  m_pScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  // 直接使用 QScrollArea 的原生滚动条。它会占用右侧布局宽度，
  // 因此不会像覆盖式滚动条那样压在头像上。
  // m_pScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_pVScrollBar = m_pScrollArea->verticalScrollBar();
  m_pVScrollBar->setObjectName("chat_view_scrollbar");
  m_pVScrollBar->setSingleStep(12);
  m_pVScrollBar->hide();

  // 原生滚动条仍然保存 QScrollArea 的真实范围和值；动画只修改它的 value。
  m_pScrollAnimation = new QPropertyAnimation(m_pVScrollBar, "value", this);
  m_pScrollAnimation->setDuration(kScrollAnimationDuration);
  m_pScrollAnimation->setEasingCurve(QEasingCurve::OutCubic);
  m_scrollTarget = m_pVScrollBar->value();

  connect(m_pVScrollBar, &QScrollBar::rangeChanged, this, &ChatView::onVScrollBarMoved);
  connect(m_pVScrollBar, &QScrollBar::rangeChanged, this, [this](int minimum, int maximum) {
    m_scrollTarget = qBound(minimum, m_scrollTarget, maximum);

    // 联系人增加或窗口大小改变后，重新判断是否需要显示。
    updateBarVisibility();
  });

  connect(m_pVScrollBar, &QScrollBar::sliderReleased, this, &ChatView::updateBarVisibility);

  connect(m_pVScrollBar, &QScrollBar::valueChanged, this, [this](int value) {
    if (m_pScrollAnimation->state() != QAbstractAnimation::Running) {
      m_scrollTarget = value;
    }
  });

  // 用户拖动的就是原生滚动条，按下滑块时停止滚轮动画，避免争抢 value。
  connect(m_pVScrollBar, &QScrollBar::sliderPressed, this, [this]() {
    m_pScrollAnimation->stop();
    m_scrollTarget = m_pVScrollBar->value();
  });

  // 滚轮事件实际发送给 viewport，因此在 viewport 上拦截并添加缓动。
  m_pScrollArea->viewport()->installEventFilter(this);
}
void ChatView::appendChatItem(QWidget *item)
{
  QVBoxLayout *vl = qobject_cast<QVBoxLayout *>(m_pScrollArea->widget()->layout());
  vl->insertWidget(vl->count() - 1, item);
  isAppended = true;
}
bool ChatView::eventFilter(QObject *o, QEvent *e)
{
  if (o == m_pScrollArea->viewport() && e->type() == QEvent::Wheel) {
    if (handleWheelEvent(static_cast<QWheelEvent *>(e))) {
      return true;
    }
  }

  return QWidget::eventFilter(o, e);
}

bool ChatView::handleWheelEvent(QWheelEvent *event)
{
  // 触控板本身提供连续的 pixelDelta，交还给 QScrollArea 原生处理。
  if (!event->pixelDelta().isNull()) {
    m_pScrollAnimation->stop();
    return false;
  }

  const int angleDeltaY = event->angleDelta().y();
  if (angleDeltaY == 0 || m_pVScrollBar->maximum() <= m_pVScrollBar->minimum()) {
    return false;
  }

  // 快速连续滚轮时，从上一次尚未到达的目标继续累加，形成连续缓动。
  if (m_pScrollAnimation->state() != QAbstractAnimation::Running) {
    m_scrollTarget = m_pVScrollBar->value();
  }

  const qreal wheelSteps = static_cast<qreal>(angleDeltaY) / 120.0;
  const int scrollDistance = qRound(wheelSteps * kWheelScrollDistance);
  m_scrollTarget
    = qBound(m_pVScrollBar->minimum(), m_scrollTarget - scrollDistance, m_pVScrollBar->maximum());

  m_pScrollAnimation->stop();
  m_pScrollAnimation->setStartValue(m_pVScrollBar->value());
  m_pScrollAnimation->setEndValue(m_scrollTarget);
  m_pScrollAnimation->start();
  event->accept();
  return true;
}

void ChatView::paintEvent(QPaintEvent *)
{
  QStyleOption opt;
  opt.initFrom(this);
  QPainter p(this);
  style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}
void ChatView::onVScrollBarMoved(int min, int max)
{
  Q_UNUSED(min);
  Q_UNUSED(max);

  if (isAppended) // 添加item可能调用多次
  {
    QScrollBar *m_pVScrollBar = m_pScrollArea->verticalScrollBar();
    m_pScrollAnimation->stop();
    m_scrollTarget = m_pVScrollBar->maximum();
    m_pVScrollBar->setSliderPosition(m_pVScrollBar->maximum());
    // 500毫秒内可能调用多次
    QTimer::singleShot(500, this, [this]() { isAppended = false; });
  }
}
void ChatView::enterEvent(QEnterEvent *event)
{
  // 先让 QListWidget 执行原本的鼠标进入处理
  QWidget::enterEvent(event);
  updateBarVisibility();
}

void ChatView::leaveEvent(QEvent *event)
{
  QWidget::leaveEvent(event);
  updateBarVisibility();
}
void ChatView::updateBarVisibility()
{
  // chat_view_scrollbar 只是 QSS 对象名，不是C++变量。
  QScrollBar *scrollBar = m_pScrollArea->verticalScrollBar();

  // maximum > minimum 表示聊天内容已经超过可视区域。
  const bool hasScrollableContent = scrollBar->maximum() > scrollBar->minimum();

  // 判断鼠标当前是否还在整个 ChatView 区域内。
  const QPoint cursorPosition = mapFromGlobal(QCursor::pos());
  const bool cursorInsideChatView = rect().contains(cursorPosition);

  /*
   * 鼠标在聊天区域内：显示
   * 鼠标已经拖到区域外，但仍按着滑块：继续显示
   * 鼠标离开且没有拖动：隐藏
   */
  const bool shouldRemainVisible = cursorInsideChatView || scrollBar->isSliderDown();

  scrollBar->setVisible(hasScrollableContent && shouldRemainVisible);
}
