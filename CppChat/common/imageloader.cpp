#include "imageloader.h"
#include "chatstore.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QImageReader>
#include <QRunnable>
#include <QThread>
#include <QTimer>
#include <QtMath>
#include <utility>

namespace {
constexpr int kDecodeConcurrency = 2;
constexpr int kPreviewCacheKiB = 16 * 1024;
constexpr qint64 kPendingBytesLimit = 64 * 1024 * 1024;

QString previewKey(quint64 generation, const QString &id, const QSize &bounds, qreal ratio)
{
  return QStringLiteral("%1:%2:%3x%4:%5")
      .arg(generation)
      .arg(id)
      .arg(bounds.width())
      .arg(bounds.height())
      .arg(qBound(qreal(1), ratio, qreal(4)), 0, 'f', 3);
}

LoadedImage decodeImage(const QByteArray &bytes, const QSize &bounds, qreal ratio)
{
  QBuffer buffer;
  buffer.setData(bytes);
  buffer.open(QIODevice::ReadOnly);
  QImageReader reader(&buffer);
  reader.setAutoTransform(true);
  QSize displaySize;
  if (!bounds.isEmpty()) {
    const auto size = reader.size();
    const bool rotated = reader.transformation().testFlag(QImageIOHandler::TransformationRotate90);
    displaySize = rotated ? size.transposed() : size;
    if (displaySize.isValid()) {
      if (displaySize.width() > bounds.width() || displaySize.height() > bounds.height())
        displaySize.scale(bounds, Qt::KeepAspectRatio);
      const QSize pixels(qMax(1, qCeil(displaySize.width() * ratio)),
                         qMax(1, qCeil(displaySize.height() * ratio)));
      const auto readSize = rotated ? pixels.transposed() : pixels;
      if (size.width() > readSize.width() || size.height() > readSize.height())
        reader.setScaledSize(readSize);
    }
  }
  LoadedImage result;
  result.image = reader.read();
  if (result.image.isNull())
    result.error = QObject::tr("图片读取失败：%1").arg(reader.errorString());
  else if (!bounds.isEmpty()) {
    if (!displaySize.isValid()) {
      displaySize = result.image.size();
      if (displaySize.width() > bounds.width() || displaySize.height() > bounds.height())
        displaySize.scale(bounds, Qt::KeepAspectRatio);
    }
    // 小图在高 DPI 下仍保持原本逻辑尺寸，不能只设 DPR 把它显示成一半大小。
    const QSize pixels(qMax(1, qCeil(displaySize.width() * ratio)),
                       qMax(1, qCeil(displaySize.height() * ratio)));
    if (result.image.size() != pixels)
      result.image = result.image.scaled(pixels, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    result.image.setDevicePixelRatio(ratio);
  }
  return result;
}
} // namespace

ImageLoader *ImageLoader::instance()
{
  static auto *loader = new ImageLoader(QCoreApplication::instance());
  return loader;
}

ImageLoader::ImageLoader(QObject *parent) : QObject(parent), _previews(kPreviewCacheKiB)
{
  _pool.setMaxThreadCount(kDecodeConcurrency);
}

ImageLoader::~ImageLoader()
{
  _pool.clear();
  // 保证工作线程在 QObject 析构前完成，不向已经销毁的调度对象投递结果。
  _pool.waitForDone();
}

QImage ImageLoader::cachedPreview(const QString &id, const QSize &bounds, qreal ratio)
{
  Q_ASSERT(QThread::currentThread() == thread());
  const auto key = previewKey(ChatStore::instance()->Generation(), id, bounds, ratio);
  const auto *image = _previews.object(key);
  return image ? *image : QImage();
}

void ImageLoader::load(const QString &id, const QByteArray &bytes, const QSize &bounds, qreal ratio,
                       QObject *receiver, Callback callback)
{
  Q_ASSERT(QThread::currentThread() == thread());
  if (!receiver || !callback)
    return;
  const auto generation = ChatStore::instance()->Generation();
  if (_generation != generation) {
    _generation = generation;
    _previews.clear();
    while (!_queue.isEmpty()) {
      const auto task = _queue.dequeue();
      _pendingBytes -= task.bytes.size();
      _listeners.remove(task.key);
    }
  }
  ratio = qBound(qreal(1), ratio, qreal(4));
  const auto key = previewKey(generation, id, bounds, ratio);
  if (!bounds.isEmpty()) {
    if (const auto *image = _previews.object(key)) {
      const LoadedImage result{*image, {}};
      QTimer::singleShot(0, receiver, [generation, callback, result] {
        if (ChatStore::instance()->Generation() == generation)
          callback(result);
      });
      return;
    }
  }
  if (_listeners.contains(key)) {
    _listeners[key].append({receiver, std::move(callback)});
    return;
  }
  if (bytes.isEmpty() || _pendingBytes + bytes.size() > kPendingBytesLimit) {
    QTimer::singleShot(0, receiver, [generation, callback] {
      if (ChatStore::instance()->Generation() == generation)
        callback({{}, QObject::tr("图片处理队列繁忙或数据为空，请稍后重试")});
    });
    return;
  }
  _listeners.insert(key, {{receiver, std::move(callback)}});
  _pendingBytes += bytes.size();
  _queue.enqueue({key, bytes, bounds, ratio, generation});
  startNext();
}

void ImageLoader::startNext()
{
  while (_active < kDecodeConcurrency && !_queue.isEmpty()) {
    const auto task = _queue.dequeue();
    bool wanted = false;
    for (const auto &listener : _listeners.value(task.key))
      wanted = wanted || !listener.receiver.isNull();
    if (!wanted || task.generation != ChatStore::instance()->Generation()) {
      _pendingBytes -= task.bytes.size();
      _listeners.remove(task.key);
      continue;
    }
    ++_active;
    _pool.start(QRunnable::create([this, task] {
      const auto result = decodeImage(task.bytes, task.bounds, task.ratio);
      QMetaObject::invokeMethod(
          this,
          [this, task, result] {
            --_active;
            _pendingBytes -= task.bytes.size();
            const auto listeners = _listeners.take(task.key);
            if (ChatStore::instance()->Generation() == task.generation) {
              if (!result.image.isNull() && !task.bounds.isEmpty()) {
                const auto cost = qMax(qint64(1), (result.image.sizeInBytes() + 1023) / 1024);
                _previews.insert(task.key, new QImage(result.image), int(cost));
              }
              for (const auto &listener : listeners)
                if (listener.receiver)
                  listener.callback(result);
            }
            startNext();
          },
          Qt::QueuedConnection);
    }));
  }
}
