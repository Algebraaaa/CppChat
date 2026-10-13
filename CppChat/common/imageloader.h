#pragma once

#include <QByteArray>
#include <QCache>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSize>
#include <QThreadPool>
#include <functional>

struct LoadedImage {
  QImage image;
  QString error;
};

// GUI 线程调度与缓存；工作线程只处理字节和 QImage，不访问 QWidget / UserMgr。
class ImageLoader : public QObject {
public:
  using Callback = std::function<void(const LoadedImage &)>;
  static ImageLoader *instance();
  ~ImageLoader() override;
  QImage cachedPreview(const QString &id, const QSize &bounds, qreal ratio);
  void load(const QString &id, const QByteArray &bytes, const QSize &bounds, qreal ratio,
            QObject *receiver, Callback callback);

private:
  explicit ImageLoader(QObject *parent);
  struct Listener {
    QPointer<QObject> receiver;
    Callback callback;
  };
  struct Task {
    QString key;
    QByteArray bytes;
    QSize bounds;
    qreal ratio = 1;
    quint64 generation = 0;
  };
  void startNext();
  QThreadPool _pool;
  QCache<QString, QImage> _previews;
  QHash<QString, QList<Listener>> _listeners;
  QQueue<Task> _queue;
  qint64 _pendingBytes = 0;
  int _active = 0;
  quint64 _generation = 0;
};
