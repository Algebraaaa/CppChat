#pragma once

#include <QByteArray>
#include <QCache>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <atomic>
#include <memory>

// 图片数据走独立 TCP 连接，聊天 TCP 只传文件 ID；所有阻塞 I/O 均在工作线程中运行。
class ResourceMgr : public QObject {
  Q_OBJECT
public:
  static ResourceMgr *instance();
  ~ResourceMgr() override;
  void ResetSession();
  void uploadImage(const QString &path, int recipient, const QString &requestId,
                   int groupThread = 0);
  void uploadImage(const QByteArray &image, int recipient, const QString &requestId,
                   int groupThread = 0);
  void fetchImage(const QString &fileId);
  void rememberImage(const QString &fileId, const QByteArray &bytes);
  QByteArray cachedImage(const QString &fileId) const;
signals:
  void uploadProgress(const QString &requestId, qint64 done, qint64 total);
  void uploadFinished(const QString &requestId, const QString &fileId, const QString &error);
  void downloadProgress(const QString &fileId, qint64 done, qint64 total);
  void imageReady(const QString &fileId, const QByteArray &bytes, const QString &error);

private:
  explicit ResourceMgr(QObject *parent = nullptr);
  void downloadImage(const QString &fileId);
  quint64 _cacheGeneration = 0;
  void startUpload(const QString &path, const QByteArray &bytes, int recipient,
                   const QString &requestId, int groupThread);
  QCache<QString, QByteArray> _cache;
  QHash<QString, QByteArray> _partials;
  QSet<QString> _fetching;
  QThreadPool _downloadPool;
  QThreadPool _uploadPool;
  int _downloadTasks = 0;
  int _uploadTasks = 0;
  std::shared_ptr<std::atomic_bool> _cancelled = std::make_shared<std::atomic_bool>(false);
};
