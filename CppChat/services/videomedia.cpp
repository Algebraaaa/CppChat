#include "videomedia.h"
#include <QBuffer>
#include <QCamera>
#include <QCameraDevice>
#include <QImageReader>
#include <QImageWriter>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVideoFrame>
#include <QVideoSink>

VideoMedia::VideoMedia(QObject *parent) : QObject(parent) {}
VideoMedia::~VideoMedia()
{
  disconnect(this, nullptr, nullptr, nullptr);
  stop();
}
QByteArray VideoMedia::encodeFrame(const QImage &image)
{
  if (image.isNull())
    return {};
  for (const auto size : {QSize(Width, Height), QSize(240, 180), QSize(160, 120)}) {
    const auto frame = image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_RGB32);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "JPEG");
    writer.setQuality(40);
    if (!writer.write(frame))
      return {};
    if (bytes.size() <= MaxFrameBytes)
      return bytes;
  }
  return {}; // A noisy frame must not enlarge the transport or block audio.
}
QImage VideoMedia::decodeFrame(const QByteArray &jpeg)
{
  if (jpeg.isEmpty() || jpeg.size() > MaxFrameBytes)
    return {};
  QBuffer buffer;
  buffer.setData(jpeg);
  buffer.open(QIODevice::ReadOnly);
  QImageReader reader(&buffer, "JPEG");
  reader.setAutoDetectImageFormat(false);
  const auto size = reader.size();
  if (size.width() <= 0 || size.height() <= 0 || size.width() > Width || size.height() > Height)
    return {}; // Validate dimensions before the decoder allocates a pixel buffer.
  return reader.read();
}
bool VideoMedia::start(QString &error)
{
  stop();
  const auto device = QMediaDevices::defaultVideoInput();
  if (device.isNull()) {
    error = tr("找不到摄像头，请检查设备及系统摄像头权限");
    return false;
  }
  camera_ = new QCamera(device, this);
  auto formats = device.videoFormats();
  QCameraFormat selected;
  for (const auto &format : formats)
    if (!format.resolution().isEmpty() &&
        (selected.isNull() || format.resolution().width() * format.resolution().height() <
                                  selected.resolution().width() * selected.resolution().height()))
      selected = format;
  if (!selected.isNull())
    camera_->setCameraFormat(selected);
  capture_ = new QMediaCaptureSession(this);
  sink_ = new QVideoSink(this);
  capture_->setCamera(camera_);
  capture_->setVideoSink(sink_);
  const auto generation = generation_;
  connect(camera_, &QCamera::errorOccurred, this,
          [this, generation](QCamera::Error code, const QString &message) {
            if (generation == generation_ && camera_ && code != QCamera::NoError)
              emit failed(tr("摄像头不可用：%1").arg(message));
          });
  connect(camera_, &QCamera::activeChanged, this, [this, generation](bool active) {
    if (generation == generation_ && camera_ && !active)
      emit failed(tr("摄像头已停止或被移除"));
  });
  connect(
      sink_, &QVideoSink::videoFrameChanged, this, [this, generation](const QVideoFrame &frame) {
        if (generation != generation_ || !camera_ || !frame.isValid() ||
            (lastFrame_.isValid() && lastFrame_.elapsed() < FrameInterval))
          return;
        lastFrame_.start();
        const auto image = frame.toImage();
        const auto bytes = encodeFrame(image);
        if (!bytes.isEmpty()) {
          emit preview(image.scaled(Width, Height, Qt::KeepAspectRatio, Qt::SmoothTransformation));
          emit encoded(bytes);
        }
      });
  camera_->start();
  return true;
}
void VideoMedia::stop()
{
  ++generation_;
  auto *camera = camera_;
  camera_ = nullptr;
  if (camera) {
    disconnect(camera, nullptr, this, nullptr);
    camera->stop();
    capture_->setCamera(nullptr);
    capture_->setVideoSink(nullptr);
    camera->deleteLater();
    capture_->deleteLater();
    sink_->deleteLater();
  }
  capture_ = nullptr;
  sink_ = nullptr;
  lastFrame_.invalidate();
  emit preview({});
}
