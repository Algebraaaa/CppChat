#include "screencapture.h"
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <QtMath>
#include <functional>
#include <utility>

namespace {
// 框选坐标是逻辑像素，原图是物理像素；不要用缩略图或逻辑坐标直接裁剪原图。
QRect capturePixelRect(const QRectF &selection, const QSize &logicalSize, const QSize &pixelSize)
{
  if (logicalSize.isEmpty() || pixelSize.isEmpty())
    return {};
  const QRectF clipped = selection.normalized().intersected(QRectF(QPointF(), logicalSize));
  if (clipped.isEmpty())
    return {};
  const qreal scaleX = qreal(pixelSize.width()) / logicalSize.width();
  const qreal scaleY = qreal(pixelSize.height()) / logicalSize.height();
  const int left = qFloor(clipped.left() * scaleX);
  const int top = qFloor(clipped.top() * scaleY);
  const int right = qCeil(clipped.right() * scaleX);
  const int bottom = qCeil(clipped.bottom() * scaleY);
  return QRect(left, top, right - left, bottom - top).intersected(QRect(QPoint(), pixelSize));
}

class CaptureOverlay : public QWidget
{
public:
  CaptureOverlay(QImage image, std::function<void(const QImage &)> accept,
                 std::function<void()> cancel, std::function<void(CaptureOverlay *)> beginSelection)
      : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
        _image(std::move(image)), _accept(std::move(accept)), _cancel(std::move(cancel)),
        _beginSelection(std::move(beginSelection))
  {
    setObjectName("capture_overlay");
    setAttribute(Qt::WA_QuitOnClose, false);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);
    _image.setDevicePixelRatio(1);

    _toolbar = new QFrame(this);
    _toolbar->setObjectName("capture_toolbar");
    _toolbar->setAttribute(Qt::WA_StyledBackground);
    _toolbar->setCursor(Qt::ArrowCursor);
    auto *layout = new QHBoxLayout(_toolbar);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    const auto addButton = [&](const QString &text, const char *role) {
      auto *button = new QPushButton(text, _toolbar);
      button->setProperty("buttonRole", role);
      button->setCursor(Qt::PointingHandCursor);
      button->setFixedHeight(30);
      layout->addWidget(button);
      return button;
    };
    connect(addButton(tr("重新框选"), "secondary"), &QPushButton::clicked, this, [this] {
      clearSelection();
      setFocus();
    });
    connect(addButton(tr("取消"), "secondary"), &QPushButton::clicked, this, [this] { _cancel(); });
    connect(addButton(tr("完成"), "primary"), &QPushButton::clicked, this, [this] { confirm(); });
    _toolbar->hide();
  }

  void clearSelection()
  {
    _dragging = false;
    _moving = false;
    _selection = {};
    _toolbar->hide();
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter painter(this);
    painter.drawImage(rect(), _image);
    QPainterPath mask;
    mask.addRect(QRectF(rect()));
    if (!_selection.isEmpty())
      mask.addRect(_selection);
    painter.fillPath(mask, QColor(0, 0, 0, 105));
    painter.setRenderHint(QPainter::Antialiasing);
    if (_selection.isEmpty()) {
      drawHint(painter, tr("拖动鼠标框选 · Esc 或右键取消"), QRect(16, 24, width() - 32, 32));
      return;
    }
    painter.setPen(QPen(QColor("#07c160"), 1.5));
    painter.drawRect(_selection);
    const QSize pixels = capturePixelRect(_selection, size(), _image.size()).size();
    const QString dimensions = tr("%1 × %2 px").arg(pixels.width()).arg(pixels.height());
    const int labelWidth = painter.fontMetrics().horizontalAdvance(dimensions) + 20;
    const int labelX = qBound(4, qRound(_selection.left()), qMax(4, width() - labelWidth - 4));
    const int labelY =
        _selection.top() >= 32 ? qRound(_selection.top()) - 30 : qRound(_selection.top()) + 4;
    drawHint(painter, dimensions, QRect(labelX, labelY, labelWidth, 26));
  }

  void mousePressEvent(QMouseEvent *event) override
  {
    if (event->button() == Qt::RightButton) {
      _cancel();
      return;
    }
    if (event->button() != Qt::LeftButton)
      return;
    _beginSelection(this);
    _origin = boundedPosition(event->position());
    if (_selection.contains(_origin)) {
      _moving = true;
      _previousSelection = _selection;
      _toolbar->hide();
    } else {
      clearSelection();
    }
    _dragging = true;
    setFocus();
  }

  void mouseMoveEvent(QMouseEvent *event) override
  {
    if (!_dragging)
      return;
    updateSelection(event->position());
    update();
  }

  void mouseReleaseEvent(QMouseEvent *event) override
  {
    if (event->button() != Qt::LeftButton || !_dragging)
      return;
    updateSelection(event->position());
    _dragging = false;
    _moving = false;
    if (_selection.width() < 2 || _selection.height() < 2) {
      clearSelection();
      return;
    }
    _toolbar->adjustSize();
    const int x = qBound(4, qRound(_selection.right()) - _toolbar->width(),
                         qMax(4, width() - _toolbar->width() - 4));
    int y = qRound(_selection.bottom()) + 8;
    if (y + _toolbar->height() > height() - 4)
      y = qMax(4, qRound(_selection.bottom()) - _toolbar->height() - 8);
    _toolbar->move(x, y);
    _toolbar->show();
    update();
  }

  void mouseDoubleClickEvent(QMouseEvent *event) override
  {
    if (event->button() == Qt::LeftButton && _selection.contains(event->position())) {
      _dragging = false;
      confirm();
    }
  }

  void keyPressEvent(QKeyEvent *event) override
  {
    if (event->key() == Qt::Key_Escape)
      _cancel();
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
      confirm();
    else
      QWidget::keyPressEvent(event);
  }

  void closeEvent(QCloseEvent *event) override
  {
    event->ignore();
    _cancel();
  }

private:
  void updateSelection(const QPointF &position)
  {
    if (!_moving) {
      _selection = QRectF(_origin, boundedPosition(position)).normalized();
      return;
    }
    const QPointF offset = boundedPosition(position) - _origin;
    const qreal x =
        qBound(0.0, _previousSelection.left() + offset.x(), width() - _previousSelection.width());
    const qreal y =
        qBound(0.0, _previousSelection.top() + offset.y(), height() - _previousSelection.height());
    _selection.moveTopLeft({x, y});
  }

  QPointF boundedPosition(const QPointF &position) const
  {
    return {qBound(0.0, position.x(), qreal(width())), qBound(0.0, position.y(), qreal(height()))};
  }

  void drawHint(QPainter &painter, const QString &text, const QRect &area)
  {
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(32, 35, 38, 215));
    painter.drawRoundedRect(area, 5, 5);
    painter.setPen(Qt::white);
    painter.drawText(area, Qt::AlignCenter, text);
  }

  void confirm()
  {
    if (_dragging || _selection.width() < 2 || _selection.height() < 2)
      return;
    const QRect pixels = capturePixelRect(_selection, size(), _image.size());
    if (!pixels.isEmpty())
      _accept(_image.copy(pixels));
  }

  QImage _image;
  QFrame *_toolbar = nullptr;
  QPointF _origin;
  QRectF _selection;
  QRectF _previousSelection;
  bool _dragging = false;
  bool _moving = false;
  std::function<void(const QImage &)> _accept;
  std::function<void()> _cancel;
  std::function<void(CaptureOverlay *)> _beginSelection;
};
} // namespace

ScreenCapture::ScreenCapture(QWidget *windowToHide, QObject *parent)
    : QObject(parent), _window(windowToHide)
{
  connect(qApp, &QGuiApplication::screenRemoved, this, [this] { cancel(); });
  connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
    _restoreWindow = false;
    cancel();
  });
}

ScreenCapture::~ScreenCapture()
{
  clearOverlays();
}

void ScreenCapture::start()
{
  if (_active)
    return;
  _active = true;
  _restoreWindow = _window && _window->isVisible();
  if (_restoreWindow)
    _window->hide();
  // 等桌面合成器完成重绘，避免把聊天窗口或工具提示截进去。
  QTimer::singleShot(180, this, [this] {
    if (_active)
      captureScreens();
  });
}

void ScreenCapture::captureScreens()
{
  const auto screens = QGuiApplication::screens();
  if (screens.isEmpty()) {
    finish({}, tr("未找到可截屏的显示器"));
    return;
  }
  QVector<QImage> images;
  // 必须先抓取所有原图，再显示任何遮罩窗口。
  for (auto *screen : screens) {
    const auto image = screen->grabWindow(0).toImage();
    if (image.isNull()) {
      finish({}, tr("无法获取屏幕图像，请检查系统截屏权限"));
      return;
    }
    images.append(image);
  }
  CaptureOverlay *focusedOverlay = nullptr;
  const QPointer<ScreenCapture> guard(this);
  for (int i = 0; i < screens.size(); ++i) {
    auto *screen = screens[i];
    auto *overlay = new CaptureOverlay(
        images[i],
        [guard](const QImage &image) {
          if (guard)
            guard->finish(image);
        },
        [guard] {
          if (guard)
            guard->cancel();
        },
        [guard](CaptureOverlay *selected) {
          if (!guard)
            return;
          for (const auto &other : guard->_overlays)
            if (other && other != selected)
              static_cast<CaptureOverlay *>(other.data())->clearSelection();
        });
    _overlays.append(overlay);
    // 各屏幕使用独立窗口，避免混合 DPI 下跨屏窗口采用同一个缩放比例。
    overlay->winId();
    overlay->windowHandle()->setScreen(screen);
    overlay->setGeometry(screen->geometry());
    overlay->showFullScreen();
    if (screen->geometry().contains(QCursor::pos()))
      focusedOverlay = overlay;
  }
  if (!focusedOverlay)
    focusedOverlay = static_cast<CaptureOverlay *>(_overlays.front().data());
  focusedOverlay->raise();
  focusedOverlay->activateWindow();
  focusedOverlay->setFocus();
}

void ScreenCapture::cancel()
{
  finish();
}

void ScreenCapture::clearOverlays()
{
  for (const auto &overlay : _overlays) {
    if (!overlay)
      continue;
    overlay->hide();
    // 回调可能由当前遮罩的事件处理触发，延迟释放以免事件仍在执行。
    overlay->deleteLater();
  }
  _overlays.clear();
}

void ScreenCapture::finish(const QImage &image, const QString &error)
{
  if (!_active)
    return;
  _active = false;
  clearOverlays();
  if (_restoreWindow && _window) {
    _window->show();
    _window->raise();
    _window->activateWindow();
  }
  _restoreWindow = false;
  if (!error.isEmpty())
    emit failed(error);
  else if (!image.isNull())
    emit captured(image);
  emit finished();
}
