#include "app/mainwindow.h"
#include "dialogs/chatdialog.h"
#include "dialogs/logindialog.h"
#include "dialogs/registerdialog.h"
#include "dialogs/resetdialog.h"
#include "network/tcpmgr.h"
#include "ui_mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QIcon>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QWindow>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

namespace {
constexpr int kResizeBorderWidth = 6; // 鼠标距窗口边缘多少逻辑像素时可以缩放。

#ifdef Q_OS_WIN
// Windows 11 圆角属性；使用数值以兼容尚未声明这些枚举的旧版 MinGW。
constexpr DWORD kWindowCornerPreference = 33; // DWMWA_WINDOW_CORNER_PREFERENCE
constexpr DWORD kRoundCorner = 2;            // DWMWCP_ROUND
#endif
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
  ui->setupUi(this);

  // Qt 使用自定义标题栏；Windows 的原生窗口样式在下面单独补回，保留系统动画。
  setWindowFlag(Qt::FramelessWindowHint, true);

  setupTitleBar();
  setupPages();
  connectPageSignals();
  setupMouseTracking();

#ifdef Q_OS_WIN
  // 创建 Windows 窗口句柄 HWND，再设置样式和圆角；此处不会显示窗口。
  winId();
  updateNativeWindowFrame();
#endif
}

MainWindow::~MainWindow()
{
  qApp->removeEventFilter(this);
  delete ui;
}

void MainWindow::setupTitleBar()
{
  ui->minimize_btn->setIcon(QIcon(QStringLiteral(":/res/window/minimize.svg")));
  updateCloseButtonIcon(false);
  updateMaximizeButton();
  for (QPushButton *button : {ui->minimize_btn, ui->maximize_btn, ui->close_btn}) {
    button->setIconSize(QSize(12, 12));
  }
  ui->minimize_btn->setAccessibleName(QStringLiteral("最小化"));
  ui->close_btn->setAccessibleName(QStringLiteral("关闭"));

  const QIcon appIcon(QStringLiteral(":/res/app.ico"));
  setWindowIcon(appIcon);
  ui->app_icon->setPixmap(appIcon.pixmap(20, 20));

  // 标题文字和图标不截获鼠标，让它们后面的 title_bar 统一处理拖动。
  ui->app_icon->setAttribute(Qt::WA_TransparentForMouseEvents);
  ui->title_label->setAttribute(Qt::WA_TransparentForMouseEvents);

  connect(ui->minimize_btn, &QPushButton::clicked, this,
          &MainWindow::showMinimized);
  connect(ui->maximize_btn, &QPushButton::clicked, this,
          &MainWindow::toggleMaximizeRestore);
  connect(ui->close_btn, &QPushButton::clicked, this, &MainWindow::close);
}

void MainWindow::setupPages()
{
  _loginDialog = new LoginDialog(this);
  _registerDialog = new RegisterDialog(this);
  _resetDialog = new ResetDialog(this);
  _chatDialog = new ChatDialog(this);
  const QSize chatDesignSize = _chatDialog->size();

  _pages = new QStackedWidget(ui->content_host);
  _pages->addWidget(_loginDialog);
  _pages->addWidget(_registerDialog);
  _pages->addWidget(_resetDialog);
  _pages->addWidget(_chatDialog);
  ui->content_layout->addWidget(_pages);

  // 临时直接测试聊天主页：绕过 HTTP/TCP 登录流程。
  // 恢复登录入口时，将下面的 _chatDialog 改为 _loginDialog。
  setMinimumSize(0, 0);
  setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
  _pages->setMinimumSize(chatDesignSize);
  _pages->setCurrentWidget(_chatDialog);
  resize(1000, 700);
}

void MainWindow::connectPageSignals()
{
  // [this] 让 lambda 可以访问本窗口的页面成员。
  connect(_loginDialog, &LoginDialog::switchRegister, this,
          [this]() { _pages->setCurrentWidget(_registerDialog); });
  connect(_registerDialog, &RegisterDialog::switchLogin, this,
          [this]() { _pages->setCurrentWidget(_loginDialog); });
  connect(_loginDialog, &LoginDialog::switchReset, this,
          [this]() { _pages->setCurrentWidget(_resetDialog); });
  connect(_resetDialog, &ResetDialog::switchLogin, this,
          [this]() { _pages->setCurrentWidget(_loginDialog); });

  // 正常登录流程：TCP 登录成功后，网络模块通知主窗口进入聊天页。
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_swich_chatdlg, this,
          [this]() { _pages->setCurrentWidget(_chatDialog); });
}

void MainWindow::setupMouseTracking()
{
  // 不按鼠标键时也接收移动事件，用来更新窗口边缘的缩放光标。
  setMouseTracking(true);
  const auto childWidgets = findChildren<QWidget *>();
  for (QWidget *child : childWidgets) {
    child->setMouseTracking(true);
  }
  // 子控件也会收到鼠标事件，所以在应用层监听，再按所属窗口过滤。
  qApp->installEventFilter(this);
}

bool MainWindow::event(QEvent *event)
{
  const bool handled = QMainWindow::event(event);
  // 窗口句柄重建或窗口重新显示后，重新应用原生样式和圆角。
  if (event->type() == QEvent::WinIdChange || event->type() == QEvent::Show) {
    updateNativeWindowFrame();
  }
  return handled;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
  auto *watchedWidget = qobject_cast<QWidget *>(watched);
  if (!watchedWidget || watchedWidget->window() != this) {
    return QMainWindow::eventFilter(watched, event);
  }

  if (watched == ui->close_btn) {
    if (event->type() == QEvent::Enter) {
      updateCloseButtonIcon(true);
    } else if (event->type() == QEvent::Leave) {
      updateCloseButtonIcon(false);
    }
  }

  if (event->type() == QEvent::MouseMove) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->buttons() == Qt::NoButton) {
      updateResizeCursor(resizeEdgesAt(mouseEvent->globalPosition().toPoint()));
    }
  }

  // 边缘缩放优先于标题栏拖动，避免顶部边缘被当成移动区域。
  if (event->type() == QEvent::MouseButtonPress) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() == Qt::LeftButton && !isMaximized()) {
      const Qt::Edges edges =
          resizeEdgesAt(mouseEvent->globalPosition().toPoint());
      if (edges != Qt::Edges() && windowHandle() != nullptr
          && windowHandle()->startSystemResize(edges)) {
        return true;
      }
    }
  }

  // 双击标题栏切换最大化；按住标题栏启动系统拖动。
  if (watched == ui->title_bar
      && event->type() == QEvent::MouseButtonDblClick) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() == Qt::LeftButton) {
      toggleMaximizeRestore();
      return true;
    }
  }

  if (watched == ui->title_bar
      && event->type() == QEvent::MouseButtonPress) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() == Qt::LeftButton && windowHandle() != nullptr
        && windowHandle()->startSystemMove()) {
      return true;
    }
  }

  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent *event)
{
  QMainWindow::changeEvent(event);

  if (event->type() == QEvent::WindowStateChange) {
    updateMaximizeButton();
    unsetCursor();
  }
}

void MainWindow::toggleMaximizeRestore()
{
#ifdef Q_OS_WIN
  // Qt 6.5 的 FramelessWindowHint 分支用 MoveWindow 直接改变几何尺寸，
  // 绕过了系统最大化/还原动画。这里直接请求原生状态切换；Qt 收到
  // WM_SIZE 后会同步窗口状态，并通过 changeEvent 更新按钮图标。
  updateNativeWindowFrame();
  const HWND hwnd = reinterpret_cast<HWND>(winId());
  ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
#else
  if (isMaximized()) {
    showNormal();
  } else {
    showMaximized();
  }
#endif

  updateMaximizeButton();
}

void MainWindow::updateMaximizeButton()
{
  if (isMaximized()) {
    ui->maximize_btn->setIcon(QIcon(QStringLiteral(":/res/window/restore.svg")));
    ui->maximize_btn->setToolTip(QStringLiteral("还原"));
  } else {
    ui->maximize_btn->setIcon(QIcon(QStringLiteral(":/res/window/maximize.svg")));
    ui->maximize_btn->setToolTip(QStringLiteral("最大化"));
  }
  ui->maximize_btn->setAccessibleName(ui->maximize_btn->toolTip());
}

void MainWindow::updateCloseButtonIcon(bool hovered)
{
  // QSS 的 color 只改变文字；红色悬停背景需要单独配白色 SVG。
  const QString iconPath = hovered
      ? QStringLiteral(":/res/window/close-white.svg")
      : QStringLiteral(":/res/window/close.svg");
  ui->close_btn->setIcon(QIcon(iconPath));
}

void MainWindow::updateNativeWindowFrame()
{
#ifdef Q_OS_WIN
  // internalWinId() 只读取已有句柄，不会递归创建窗口。
  const HWND hwnd = reinterpret_cast<HWND>(internalWinId());
  if (!hwnd || isFullScreen()) {
    return;
  }

  // 保留原生标题栏和可缩放样式，让 Windows 提供窗口动画。
  // 原生标题栏的可见区域由 nativeEvent 中的 WM_NCCALCSIZE 隐藏。
  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  const LONG_PTR nativeStyle =
      style | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
  if (style != nativeStyle) {
    SetWindowLongPtrW(hwnd, GWL_STYLE, nativeStyle);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
                     | SWP_FRAMECHANGED);
  }

  // 即使窗口样式没有变化，也要设置圆角。最大化时系统会自动使用直角。
  // Windows 10 不支持此属性；调用失败时保留原有窗口外观。
  const HRESULT result = DwmSetWindowAttribute(
      hwnd, kWindowCornerPreference, &kRoundCorner, sizeof(kRoundCorner));
  if (FAILED(result)) {
    qWarning() << "Failed to set window corner preference:" << result;
  }
#endif
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message,
                            qintptr *result)
{
  auto *msg = static_cast<MSG *>(message);
  if (!msg || !result) {
    return QMainWindow::nativeEvent(eventType, message, result);
  }
  if (msg->message == WM_NCCALCSIZE) {
    // 客户区覆盖原生标题栏，画面仍使用 Designer 中的 title_bar。
    // 两种 wParam 对应不同的结构，不能一律当作 NCCALCSIZE_PARAMS。
    RECT *clientRect = msg->wParam
        ? &reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam)->rgrc[0]
        : reinterpret_cast<RECT *>(msg->lParam);
    if (IsZoomed(msg->hwnd) && !isFullScreen()) {
      // 最大化时 Windows 会把不可见边框放到屏幕外；裁到工作区，避免
      // 顶部按钮被切掉，也避免覆盖任务栏。这里全部使用原生像素坐标。
      MONITORINFO monitorInfo{};
      monitorInfo.cbSize = sizeof(monitorInfo);
      const HMONITOR monitor = MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST);
      if (GetMonitorInfoW(monitor, &monitorInfo)) {
        RECT visibleRect{};
        if (IntersectRect(&visibleRect, clientRect, &monitorInfo.rcWork)) {
          *clientRect = visibleRect;
        }
      }
    }
    *result = 0;
    return true;
  }
  if (msg->message == WM_NCACTIVATE) {
    // 保留系统激活处理，但不让它重新绘制原生标题栏。
    *result = DefWindowProcW(msg->hwnd, msg->message, msg->wParam, -1);
    return true;
  }
  return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

Qt::Edges MainWindow::resizeEdgesAt(const QPoint &globalPosition) const
{
  if (isMaximized() || isFullScreen()) {
    return Qt::Edges();
  }

  const QPoint localPosition = mapFromGlobal(globalPosition);
  if (!rect().contains(localPosition)) {
    return Qt::Edges();
  }

  Qt::Edges edges;
  if (localPosition.x() < kResizeBorderWidth) {
    edges |= Qt::LeftEdge;
  } else if (localPosition.x() >= width() - kResizeBorderWidth) {
    edges |= Qt::RightEdge;
  }

  if (localPosition.y() < kResizeBorderWidth) {
    edges |= Qt::TopEdge;
  } else if (localPosition.y() >= height() - kResizeBorderWidth) {
    edges |= Qt::BottomEdge;
  }

  return edges;
}

void MainWindow::updateResizeCursor(Qt::Edges edges)
{
  const bool left = edges.testFlag(Qt::LeftEdge);
  const bool right = edges.testFlag(Qt::RightEdge);
  const bool top = edges.testFlag(Qt::TopEdge);
  const bool bottom = edges.testFlag(Qt::BottomEdge);

  // 角落使用斜向光标，单条边使用水平或垂直光标。
  if ((left && top) || (right && bottom)) {
    setCursor(Qt::SizeFDiagCursor);
  } else if ((right && top) || (left && bottom)) {
    setCursor(Qt::SizeBDiagCursor);
  } else if (left || right) {
    setCursor(Qt::SizeHorCursor);
  } else if (top || bottom) {
    setCursor(Qt::SizeVerCursor);
  } else {
    unsetCursor();
  }
}
