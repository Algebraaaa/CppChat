#ifndef ATTACHMENTPREPARER_H
#define ATTACHMENTPREPARER_H

#include <QByteArray>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QThreadPool>
#include <QVector>
#include <functional>
#include <memory>

// 字段只在 GUI 线程读写。MsgInfo 持有状态，使未完成的图片跟随原草稿。
struct PreparedAttachment {
  QString id;
  QByteArray bytes;
  QImage preview;
  QString error;
  bool ready = false;
};

class AttachmentPreparer : public QObject {
  Q_OBJECT
public:
  static AttachmentPreparer *instance();
  ~AttachmentPreparer() override;
  std::shared_ptr<PreparedAttachment> FromFile(const QString &path);
  std::shared_ptr<PreparedAttachment> FromDocument(const QString &path);
  std::shared_ptr<PreparedAttachment> FromImage(const QImage &image);
  std::shared_ptr<PreparedAttachment> FromBytes(const QByteArray &bytes);
  // 只用于关闭账号/程序：完成已接受的任务，再保存原账号草稿。
  void FinishPending();

signals:
  void prepared(const QString &id);

private:
  explicit AttachmentPreparer(QObject *parent);
  struct Result {
    QByteArray bytes;
    QImage preview;
    QString error;
  };
  struct Completion {
    std::shared_ptr<PreparedAttachment> attachment;
    Result result;
    qint64 cost = 0;
  };
  std::shared_ptr<PreparedAttachment> Start(std::function<Result()> work, qint64 cost);
  void DrainCompleted();
  QThreadPool _pool;
  QMutex _completedMutex;
  QVector<Completion> _completed;
  qint64 _pendingBytes = 0;
  int _tasks = 0;
};

#endif
