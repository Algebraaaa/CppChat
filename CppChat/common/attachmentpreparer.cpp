#include "attachmentpreparer.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QImageReader>
#include <QMutexLocker>
#include <QRunnable>
#include <QThread>
#include <QUuid>
#include <exception>

namespace {
constexpr qint64 kMaximumBytes = 20 * 1024 * 1024;
constexpr qint64 kMaximumPixels = 16 * 1024 * 1024;
constexpr qint64 kPendingBudget = 128 * 1024 * 1024;
constexpr int kMaximumTasks = 16;
const QSize kPreviewSize(120, 80);

// PNG 输出边写边限制，不能先生成无限大的 QByteArray 再检查大小。
class LimitedBuffer : public QBuffer
{
public:
  explicit LimitedBuffer(QByteArray *bytes) : QBuffer(bytes) {}

protected:
  qint64 writeData(const char *data, qint64 length) override
  {
    if (length > kMaximumBytes - pos())
      return -1;
    return QBuffer::writeData(data, length);
  }
};

QImage previewForBytes(const QByteArray &bytes)
{
  QBuffer buffer;
  buffer.setData(bytes);
  buffer.open(QIODevice::ReadOnly);
  QImageReader reader(&buffer);
  reader.setAutoTransform(true);
  const auto size = reader.size();
  if (!size.isValid() || qint64(size.width()) * size.height() > kMaximumPixels)
    return {};
  reader.setScaledSize(size.scaled(kPreviewSize, Qt::KeepAspectRatio));
  auto preview = reader.read();
  if (!preview.isNull())
    preview = preview.scaled(kPreviewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  return preview;
}
} // namespace

AttachmentPreparer *AttachmentPreparer::instance()
{
  static auto *preparer = new AttachmentPreparer(QCoreApplication::instance());
  return preparer;
}

AttachmentPreparer::AttachmentPreparer(QObject *parent) : QObject(parent)
{
  _pool.setMaxThreadCount(2);
}

AttachmentPreparer::~AttachmentPreparer()
{
  _pool.waitForDone();
}

std::shared_ptr<PreparedAttachment> AttachmentPreparer::Start(std::function<Result()> work,
                                                              qint64 cost)
{
  Q_ASSERT(QThread::currentThread() == thread());
  auto attachment = std::make_shared<PreparedAttachment>();
  attachment->id = "cppchat-image:/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (cost <= 0 || cost > kPendingBudget - _pendingBytes || _tasks >= kMaximumTasks) {
    attachment->ready = true;
    attachment->error = tr("图片过大或处理队列繁忙，请稍后重试");
    return attachment;
  }
  ++_tasks;
  _pendingBytes += cost;
  _pool.start(QRunnable::create([this, attachment, work, cost] {
    Result result;
    try {
      result = work();
    } catch (const std::exception &) {
      result.error = tr("图片处理失败");
    }
    {
      QMutexLocker lock(&_completedMutex);
      _completed.append({attachment, std::move(result), cost});
    }
    QMetaObject::invokeMethod(this, [this] { DrainCompleted(); }, Qt::QueuedConnection);
  }));
  return attachment;
}

std::shared_ptr<PreparedAttachment> AttachmentPreparer::FromFile(const QString &path)
{
  return Start(
      [path] {
        Result result;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > kMaximumBytes) {
          result.error = tr("图片无法读取或超过 20 MB");
          return result;
        }
        const auto size = file.size();
        result.bytes = file.read(size);
        if (result.bytes.size() != size)
          result.error = tr("图片读取失败");
        else {
          result.preview = previewForBytes(result.bytes);
          if (result.preview.isNull())
            result.error = tr("图片格式无效或像素数量超过限制");
        }
        return result;
      },
      kMaximumBytes);
}

std::shared_ptr<PreparedAttachment> AttachmentPreparer::FromBytes(const QByteArray &bytes)
{
  return Start(
      [bytes] {
        Result result;
        if (bytes.isEmpty() || bytes.size() > kMaximumBytes)
          result.error = tr("图片为空或超过 20 MB");
        else {
          result.bytes = bytes;
          result.preview = previewForBytes(bytes);
          if (result.preview.isNull())
            result.error = tr("图片格式无效或像素数量超过限制");
        }
        return result;
      },
      bytes.size());
}

std::shared_ptr<PreparedAttachment> AttachmentPreparer::FromImage(const QImage &image)
{
  if (image.isNull() || qint64(image.width()) * image.height() > kMaximumPixels)
    return Start({}, 0);
  return Start(
      [image] {
        Result result;
        LimitedBuffer buffer(&result.bytes);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
          result.error = tr("图片编码失败或编码后超过 20 MB");
        else
          result.preview =
              image.scaled(kPreviewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return result;
      },
      image.sizeInBytes());
}

void AttachmentPreparer::DrainCompleted()
{
  QVector<Completion> completed;
  {
    QMutexLocker lock(&_completedMutex);
    completed.swap(_completed);
  }
  for (auto &completion : completed) {
    --_tasks;
    _pendingBytes -= completion.cost;
    auto &attachment = *completion.attachment;
    attachment.ready = true;
    attachment.error = std::move(completion.result.error);
    if (attachment.error.isEmpty()) {
      attachment.bytes = std::move(completion.result.bytes);
      attachment.preview = std::move(completion.result.preview);
    }
    emit prepared(attachment.id);
  }
}

void AttachmentPreparer::FinishPending()
{
  Q_ASSERT(QThread::currentThread() == thread());
  _pool.waitForDone();
  DrainCompleted();
}
