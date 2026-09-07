#include "chatuserlist.h"
#include <QCursor>
#include <QEasingCurve>
#include <QtMath>

namespace {
constexpr int kOverlayScrollBarWidth = 8;          // 滚动条宽度8像素
constexpr int kOverlayScrollBarRightMargin = 2;    // 右边距
constexpr int kOverlayScrollBarVerticalMargin = 3; // 上下边距
constexpr int kWheelScrollDistance = 60;           // 滚轮一格对应的滚动距离
constexpr int kScrollAnimationDuration = 400;      // 缓动动画持续时间（毫秒）
} // namespace

ChatUserList::ChatUserList(QWidget *parent) : QListWidget(parent)
{
  // 隐藏横向滚动条
  this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  // 这里也隐藏了垂直滚动条，因为我想把滚动条悬浮在联系人列表上方
  // 但是原生滚动条对象仍负责保存范围和值，鼠标滚轮也仍由 QListWidget 正常处理
  this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  // 按像素滚动
  this->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  QScrollBar *nativeScrollBar = this->verticalScrollBar();
  // 设置滚动像素步长
  nativeScrollBar->setSingleStep(12);

  // ScrollPerPixel 只规定滚动条的数值以像素变化，本身不会产生平滑动画。
  // 这里让原生滚动条的 value 从当前位置逐帧变化到目标位置。
  _scrollAnimation = new QPropertyAnimation(nativeScrollBar, "value", this);
  _scrollAnimation->setDuration(kScrollAnimationDuration);
  _scrollAnimation->setEasingCurve(QEasingCurve::OutCubic);
  _scrollTarget = nativeScrollBar->value();

  // 这里创建浮动滚动条，他本身没有滚动的逻辑，只是在界面上显示
  _overlayScrollBar = new QScrollBar(Qt::Vertical, this);
  _overlayScrollBar->setObjectName("chat_user_overlay_scrollbar");
  _overlayScrollBar->setRange(nativeScrollBar->minimum(), nativeScrollBar->maximum());
  // pageStep() 表示一大步滚多少。点击滚动条滑块旁边的空白区域，或者按 PageUp / PageDown 时
  _overlayScrollBar->setPageStep(nativeScrollBar->pageStep());
  // singleStep()表示一小步滚多少。比如滚一下鼠标滚轮、按一下方向键时
  _overlayScrollBar->setSingleStep(nativeScrollBar->singleStep());
  _overlayScrollBar->hide();

  // 联系人数量或可视区域变化后，同步浮动滚动条的范围。
  connect(nativeScrollBar, &QScrollBar::rangeChanged, this,
          [this, nativeScrollBar](int minimum, int maximum) {
            _overlayScrollBar->setRange(minimum, maximum);
            _overlayScrollBar->setPageStep(nativeScrollBar->pageStep());
            _overlayScrollBar->setSingleStep(nativeScrollBar->singleStep());
            _scrollTarget = qBound(minimum, _scrollTarget, maximum);
            updateBarVisibility();
          });

  // 滚动鼠标滚轮->QWheelEvent->原生滚动条valueChanged->浮动滚动条SetValue
  connect(nativeScrollBar, &QScrollBar::valueChanged, this,
          [this, nativeScrollBar](int value) {
            _overlayScrollBar->setValue(value);

            // 键盘、触控板或浮动滚动条改变位置时，也要更新下一次滚轮
            // 动画的起始目标；动画运行期间不能用中间值覆盖累计目标。
            if (_scrollAnimation->state() != QAbstractAnimation::Running) {
              _scrollTarget = value;
            }

            // 判断联系人列表是否真的能够滚动 && 是否滚动到最底部
            if (nativeScrollBar->maximum() > nativeScrollBar->minimum()
                && value >= nativeScrollBar->maximum()) {
              emit sig_loading_chat_user();
            }
          });

  // 用户拖动浮动滚动条->浮动滚动条valueChanged->原生滚动条setValue->原生滚动条valueChanged
  // setValue() 设置成当前已有的相同数值时，不会再次发出 valueChanged，不然会死循环
  connect(_overlayScrollBar, &QScrollBar::valueChanged, nativeScrollBar,
          &QScrollBar::setValue);

  // 用户开始拖动浮动滚动条时，停止滚轮动画，避免两者同时修改 value。
  connect(_overlayScrollBar, &QScrollBar::sliderPressed, this,
          [this, nativeScrollBar]() {
            _scrollAnimation->stop();
            _scrollTarget = nativeScrollBar->value();
          });

  connect(_overlayScrollBar, &QScrollBar::sliderReleased, this,
          &ChatUserList::updateBarVisibility);

  updateBarGeometry();
}

void ChatUserList::enterEvent(QEnterEvent *event)
{
  // 先让 QListWidget 执行原本的鼠标进入处理
  QListWidget::enterEvent(event);
  updateBarVisibility();
}

void ChatUserList::leaveEvent(QEvent *event)
{
  QListWidget::leaveEvent(event);
  updateBarVisibility();
}

// viewport 尺寸变化后，把浮动滚动条重新贴到其右侧。
void ChatUserList::resizeEvent(QResizeEvent *event)
{
  QListWidget::resizeEvent(event);

  QScrollBar *nativeScrollBar = this->verticalScrollBar();
  _overlayScrollBar->setPageStep(nativeScrollBar->pageStep());
  _overlayScrollBar->setSingleStep(nativeScrollBar->singleStep());
  updateBarGeometry();
  updateBarVisibility();
}

void ChatUserList::wheelEvent(QWheelEvent *event)
{
  QScrollBar *nativeScrollBar = this->verticalScrollBar();

  // 触控板通常会直接提供细粒度的像素位移，本身就是连续输入，继续使用
  // QListWidget 的原生处理，避免再套一层动画后产生迟滞感。
  if (!event->pixelDelta().isNull()) {
    _scrollAnimation->stop();
    QListWidget::wheelEvent(event);
    _scrollTarget = nativeScrollBar->value();
    return;
  }

  // 普通鼠标滚轮通常通过 angleDelta() 报告离散刻度；120 表示常见的一格。
  const int angleDeltaY = event->angleDelta().y();
  if (angleDeltaY == 0) {
    QListWidget::wheelEvent(event);
    return;
  }

  if (nativeScrollBar->maximum() <= nativeScrollBar->minimum()) {
    event->ignore();
    return;
  }

  // 如果上一段动画还在运行，就继续在原目标上累加距离。这样快速滚动
  // 多格时会形成一个连续动作，而不是每一格都重新停顿。
  if (_scrollAnimation->state() != QAbstractAnimation::Running) {
    _scrollTarget = nativeScrollBar->value();
  }

  const qreal wheelSteps = static_cast<qreal>(angleDeltaY) / 120.0;
  const int scrollDistance = qRound(wheelSteps * kWheelScrollDistance);
  _scrollTarget = qBound(nativeScrollBar->minimum(),
                         _scrollTarget - scrollDistance,
                         nativeScrollBar->maximum());

  _scrollAnimation->stop();
  _scrollAnimation->setStartValue(nativeScrollBar->value());
  _scrollAnimation->setEndValue(_scrollTarget);
  _scrollAnimation->start();

  // 事件已经由当前类处理，不能再让 QListWidget 重复滚动一次。
  event->accept();
}

void ChatUserList::updateBarGeometry()
{
  // viewport()->rect() 使用 viewport 自己的坐标系；滚动条的父对象是 ChatUserList，
  // 因此要先把 viewport 左上角换算到 ChatUserList 的坐标系。
  const QPoint viewportTopLeft = this->viewport()->mapTo(this, QPoint(0, 0));
  const QRect viewportRect(viewportTopLeft, this->viewport()->size());
  const int scrollBarHeight = qMax(
      0, viewportRect.height() - kOverlayScrollBarVerticalMargin * 2);
  const int scrollBarX = qMax(
      viewportRect.left(),
      viewportRect.left() + viewportRect.width() - kOverlayScrollBarWidth
          - kOverlayScrollBarRightMargin);
  const int scrollBarY = viewportRect.top() + kOverlayScrollBarVerticalMargin;

  _overlayScrollBar->setGeometry(scrollBarX, scrollBarY,
                                 kOverlayScrollBarWidth, scrollBarHeight);
  _overlayScrollBar->raise();
}

void ChatUserList::updateBarVisibility()
{
  const QScrollBar *nativeScrollBar = this->verticalScrollBar();
  // 判断联系人内容能不能滚动
  const bool hasScrollableContent =
      nativeScrollBar->maximum() > nativeScrollBar->minimum();

  // QCursor::pos()获取鼠标的全局位置（相对于屏幕左上角）
  // mapFromGlobal()换成ChatUserList内部坐标
  const QPoint cursorPosition = this->mapFromGlobal(QCursor::pos());
  // this->rect() 表示 ChatUserList 自己的内部矩形
  const bool cursorInsideList = this->rect().contains(cursorPosition);
  const bool shouldRemainVisible =
    // scrollBar->isSliderDown()表示用户当前是否正按着滑块
    cursorInsideList || _overlayScrollBar->isSliderDown();

  _overlayScrollBar->setVisible(hasScrollableContent && shouldRemainVisible);
  if (_overlayScrollBar->isVisible()) {
    // 把浮动滚动条提高到同级控件的最上层
    _overlayScrollBar->raise();
  }
}
