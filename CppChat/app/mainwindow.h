#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

/********************************************************************************
 * @file      mainwindow.h
 * @brief     主窗口
 *
 * @author    Algebra
 * @date      2026-07-22
 * @version   1.0
 *
 * @details   管理业务页面切换、自定义标题栏及 Windows 原生窗口行为。
 *
 * Copyright (c) 2026 Algebra All rights reserved.
 *******************************************************************************/
QT_BEGIN_NAMESPACE
class QStackedWidget;
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class ChatDialog;
class LoginDialog;
class RegisterDialog;
class ResetDialog;

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

protected:
  // 窗口创建/显示、子控件鼠标事件和最大化状态变化。
  bool event(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
  void changeEvent(QEvent *event) override;
#ifdef Q_OS_WIN
  bool nativeEvent(const QByteArray &eventType, void *message,
                   qintptr *result) override;
#endif

private:
  // 初始化：标题栏、业务页面、页面信号及鼠标监听。
  void setupTitleBar();
  void setupPages();
  void connectPageSignals();
  void setupMouseTracking();

  // 窗口按钮和原生窗口外观。
  void toggleMaximizeRestore();
  void updateMaximizeButton();
  void updateCloseButtonIcon(bool hovered);
  void updateNativeWindowFrame();

  // 判断鼠标靠近哪条边，并显示对应的缩放光标。
  Qt::Edges resizeEdgesAt(const QPoint &globalPosition) const;
  void updateResizeCursor(Qt::Edges edges);

  Ui::MainWindow *ui;

  // 页面加入 QStackedWidget 后由 Qt 父子关系管理，不需要逐个 delete。
  QStackedWidget *_pages = nullptr;
  LoginDialog *_loginDialog = nullptr;
  RegisterDialog *_registerDialog = nullptr;
  ResetDialog *_resetDialog = nullptr;
  ChatDialog *_chatDialog = nullptr;
};
#endif // MAINWINDOW_H
