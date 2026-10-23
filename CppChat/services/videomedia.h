#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
class QCamera;
class QMediaCaptureSession;
class QVideoSink;

// Qt owns capture and JPEG codecs. No network, account, or call state here.
class VideoMedia : public QObject {
  Q_OBJECT
public:
  static constexpr int MaxFrameBytes = 6000;
  static constexpr int Width = 320, Height = 240, FrameInterval = 125;
  explicit VideoMedia(QObject *parent = nullptr);
  ~VideoMedia() override;
  bool start(QString &error, bool preferFront = false);
  void stop();
  bool capturing() const { return camera_ != nullptr; }
  static QByteArray encodeFrame(const QImage &image);
  static QImage decodeFrame(const QByteArray &jpeg);
signals:
  void preview(QImage image);
  void encoded(QByteArray jpeg);
  void failed(QString error);

private:
  QCamera *camera_ = nullptr;
  QMediaCaptureSession *capture_ = nullptr;
  QVideoSink *sink_ = nullptr;
  QElapsedTimer lastFrame_;
  quint64 generation_ = 0;
};
