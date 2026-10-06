#ifndef SCREENCAPTURE_H
#define SCREENCAPTURE_H

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QVector>

class QWidget;

// 管理一次截屏的窗口隐藏、各显示器框选和结束后的窗口恢复。
class ScreenCapture : public QObject
{
  Q_OBJECT
public:
  explicit ScreenCapture(QWidget *windowToHide, QObject *parent = nullptr);
  ~ScreenCapture() override;
  void start();
  void cancel();

signals:
  void captured(const QImage &image);
  void failed(const QString &message);
  void finished();

private:
  void captureScreens();
  void finish(const QImage &image = {}, const QString &error = {});
  void clearOverlays();

  QPointer<QWidget> _window;
  QVector<QPointer<QWidget>> _overlays;
  bool _active = false;
  bool _restoreWindow = false;
};

#endif // SCREENCAPTURE_H
