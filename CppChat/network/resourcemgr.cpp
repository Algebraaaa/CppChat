#include "resourcemgr.h"
#include "common/chatstore.h"
#include "common/runtimepaths.h"
#include "common/securitypolicy.h"
#include "usermgr.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRunnable>
#include <QSettings>
#include <QSslError>
#include <QSslSocket>
#include <QThread>
#include <QTimer>

namespace {
constexpr int kChunkSize = 32 * 1024;
constexpr int kMaxImageSize = 20 * 1024 * 1024;
constexpr int kDownloadConcurrency = 3;
constexpr int kMaxDownloadTasks = 64;
constexpr int kMaxUploadTasks = 8;
constexpr qint64 kPartialCacheLimit = 32 * 1024 * 1024;
constexpr int kProgressIntervalMs = 100;

struct ConnectionInfo {
  QString host;
  quint16 port;
  bool tls;
  int uid;
  QString token;
};

ConnectionInfo connectionInfo()
{
  QSettings settings(RuntimePaths::configFile(), QSettings::IniFormat);
  return {settings.value("ResourceServer/host", "127.0.0.1").toString(),
          quint16(settings.value("ResourceServer/port", 8093).toUInt()),
          settings.value("ResourceServer/tls", false).toBool(), UserMgr::GetInstance()->GetUid(),
          UserMgr::GetInstance()->GetToken()};
}

bool readExactly(QTcpSocket &socket, QByteArray &bytes, int length,
                 const std::shared_ptr<std::atomic_bool> &cancelled)
{
  QElapsedTimer deadline;
  deadline.start();
  while (bytes.size() < length) {
    if (cancelled->load() || deadline.elapsed() >= 10000)
      return false;
    if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(100)) {
      if (socket.state() != QAbstractSocket::ConnectedState)
        return false;
      continue;
    }
    bytes += socket.read(length - bytes.size());
  }
  return true;
}

bool sendRequest(QTcpSocket &socket, quint16 id, const QJsonObject &request, QString &error,
                 const std::shared_ptr<std::atomic_bool> &cancelled)
{
  if (cancelled->load())
    return false;
  const QByteArray body = QJsonDocument(request).toJson(QJsonDocument::Compact);
  if (body.isEmpty() || body.size() > 60000) {
    error = QObject::tr("资源请求过大");
    return false;
  }
  QByteArray packet;
  packet.append(char(id >> 8));
  packet.append(char(id & 0xff));
  packet.append(char(body.size() >> 8));
  packet.append(char(body.size() & 0xff));
  packet += body;
  if (socket.write(packet) != packet.size()) {
    error = QObject::tr("资源服务写入超时");
    return false;
  }
  QElapsedTimer writeDeadline;
  writeDeadline.start();
  while (socket.bytesToWrite() > 0) {
    if (cancelled->load() || writeDeadline.elapsed() >= 10000 ||
        socket.state() != QAbstractSocket::ConnectedState) {
      error = QObject::tr("资源服务写入超时或传输已取消");
      return false;
    }
    socket.waitForBytesWritten(100);
  }
  return true;
}

bool receiveResponse(QTcpSocket &socket, quint16 id, QJsonObject &response, QString &error,
                     const std::shared_ptr<std::atomic_bool> &cancelled)
{
  QByteArray head;
  if (!readExactly(socket, head, 4, cancelled)) {
    error = QObject::tr("资源服务无响应");
    return false;
  }
  const quint16 responseId = (quint8(head[0]) << 8) | quint8(head[1]);
  const int size = (quint8(head[2]) << 8) | quint8(head[3]);
  if (responseId != id + 1 || size <= 0 || size > 60000) {
    error = QObject::tr("资源服务响应格式错误");
    return false;
  }
  QByteArray payload;
  if (!readExactly(socket, payload, size, cancelled)) {
    error = QObject::tr("资源数据接收超时");
    return false;
  }
  const auto document = QJsonDocument::fromJson(payload);
  if (!document.isObject()) {
    error = QObject::tr("资源服务返回了无效 JSON");
    return false;
  }
  response = document.object();
  if (!response.value("ok").toBool()) {
    error = response.value("error").toString(QObject::tr("资源传输失败"));
    return false;
  }
  return true;
}

bool exchange(QTcpSocket &socket, quint16 id, const QJsonObject &request, QJsonObject &response,
              QString &error, const std::shared_ptr<std::atomic_bool> &cancelled)
{
  return sendRequest(socket, id, request, error, cancelled) &&
         receiveResponse(socket, id, response, error, cancelled);
}

bool connectSocket(QSslSocket &socket, const ConnectionInfo &info, QString &error,
                   const std::shared_ptr<std::atomic_bool> &cancelled)
{
  if (cancelled->load())
    return false;
  if (info.uid <= 0 || info.token.isEmpty()) {
    error = QObject::tr("请先登录");
    return false;
  }
  if (!SecurityPolicy::socketAllowed(info.tls, info.host)) {
    error = QObject::tr("正式版资源连接必须启用 TLS");
    return false;
  }
  if (info.tls) {
    if (!QSslSocket::supportsSsl()) {
      error = QObject::tr("TLS 不可用，请检查客户端的 Qt TLS 插件");
      return false;
    }
    socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
    socket.setProtocol(QSsl::TlsV1_2OrLater);
    QObject::connect(&socket, &QSslSocket::sslErrors, &socket, [](const QList<QSslError> &errors) {
      for (const auto &sslError : errors)
        qWarning() << "Resource TLS certificate verification failed:" << sslError.errorString();
    });
    socket.connectToHostEncrypted(info.host, info.port);
    if (!socket.waitForEncrypted(10000)) {
      error = QObject::tr("无法建立资源服务 TLS 连接：%1").arg(socket.errorString());
      return false;
    }
  } else {
    socket.connectToHost(info.host, info.port);
    if (!socket.waitForConnected(10000)) {
      error = QObject::tr("无法连接资源服务：%1").arg(socket.errorString());
      return false;
    }
  }
  socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
  return !cancelled->load();
}

QJsonObject baseRequest(const ConnectionInfo &info, const QString &fileId)
{
  return {{"uid", info.uid}, {"token", info.token}, {"file_id", fileId}};
}

QString cacheKey(int uid, const QString &fileId)
{
  return QString::number(ChatStore::instance()->Generation()) + ':' + QString::number(uid) + ':' +
         fileId;
}
} // namespace

ResourceMgr::ResourceMgr(QObject *parent) : QObject(parent), _cache(64 * 1024)
{
  _downloadPool.setMaxThreadCount(kDownloadConcurrency);
  _uploadPool.setMaxThreadCount(1);
}

ResourceMgr::~ResourceMgr()
{
  _cancelled->store(true);
  _downloadPool.clear();
  _uploadPool.clear();
  _downloadPool.waitForDone();
  _uploadPool.waitForDone();
}

ResourceMgr *ResourceMgr::instance()
{
  static auto *manager = new ResourceMgr(QCoreApplication::instance());
  return manager;
}

QByteArray ResourceMgr::cachedImage(const QString &fileId) const
{
  const auto *bytes = _cache.object(cacheKey(UserMgr::GetInstance()->GetUid(), fileId));
  return bytes ? *bytes : QByteArray();
}

void ResourceMgr::ResetSession()
{
  _cancelled->store(true);
  _cancelled = std::make_shared<std::atomic_bool>(false);
  _downloadPool.clear();
  _uploadPool.clear();
  _downloadTasks = _uploadTasks = 0;
  _cache.clear();
  _partials.clear();
  _fetching.clear();
  _cacheGeneration = ChatStore::instance()->Generation();
}

void ResourceMgr::rememberImage(const QString &fileId, const QByteArray &bytes)
{
  const auto generation = ChatStore::instance()->Generation();
  if (_cacheGeneration != generation)
    ResetSession();
  if (bytes.isEmpty())
    return;
  const auto key = cacheKey(UserMgr::GetInstance()->GetUid(), fileId);
  const auto cost = qMax(qint64(1), (qint64(bytes.size()) + 1023) / 1024);
  if (cost <= _cache.maxCost())
    _cache.insert(key, new QByteArray(bytes), int(cost));
}

void ResourceMgr::uploadImage(const QString &path, int recipient, const QString &requestId)
{
  startUpload(path, {}, recipient, requestId);
}

void ResourceMgr::uploadImage(const QByteArray &image, int recipient, const QString &requestId)
{
  startUpload({}, image, recipient, requestId);
}

void ResourceMgr::startUpload(const QString &path, const QByteArray &bytes, int recipient,
                              const QString &requestId)
{
  rememberImage({}, {});
  const auto info = connectionInfo();
  const auto generation = ChatStore::instance()->Generation();
  if (_uploadTasks >= kMaxUploadTasks) {
    QTimer::singleShot(0, this, [this, requestId, generation] {
      if (ChatStore::instance()->Generation() == generation)
        emit uploadFinished(requestId, {}, tr("上传队列繁忙，请稍后重试"));
    });
    return;
  }
  ++_uploadTasks;
  const auto cancelled = _cancelled;
  qInfo() << "Image upload started:" << "request=" << requestId << "uid=" << info.uid
          << "recipient=" << recipient;
  _uploadPool.start(
      QRunnable::create([this, path, bytes, recipient, requestId, info, generation, cancelled] {
        if (cancelled->load())
          return;
        QString error;
        QString fileId;
        QByteArray image = bytes;
        if (!path.isEmpty()) {
          QFile file(path);
          if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > kMaxImageSize) {
            error = tr("图片无法读取或超过 20 MB");
          } else {
            image = file.readAll();
            if (image.size() != file.size())
              error = tr("图片读取失败");
          }
        }
        if (error.isEmpty() && (image.isEmpty() || image.size() > kMaxImageSize))
          error = tr("图片为空或超过 20 MB");
        if (error.isEmpty()) {
          const QByteArray identity =
              QByteArray::number(info.uid) + ':' + QByteArray::number(recipient) + ':' +
              QCryptographicHash::hash(image, QCryptographicHash::Sha256).toHex();
          fileId = QString::fromLatin1(
              QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(32));
        }
        if (cancelled->load())
          return;
        if (error.isEmpty()) {
          QSslSocket socket;
          if (connectSocket(socket, info, error, cancelled)) {
            auto init = baseRequest(info, fileId);
            init.insert("recipient", recipient);
            init.insert("size", image.size());
            init.insert("sha256",
                        QString::fromLatin1(
                            QCryptographicHash::hash(image, QCryptographicHash::Sha256).toHex()));
            QJsonObject reply;
            if (exchange(socket, 1050, init, reply, error, cancelled)) {
              qint64 offset = reply.value("offset").toVariant().toLongLong();
              if (offset < 0 || offset > image.size())
                error = tr("资源服务返回了无效断点");
              QElapsedTimer progressTimer;
              progressTimer.start();
              // 旧服务端没有协商字段时保持单分块模式。窗口有界，不改变分块大小。
              const int window = qBound(1, reply.value("upload_window").toInt(1), 4);
              while (error.isEmpty() && offset < image.size()) {
                if (cancelled->load())
                  return;
                QVector<qint64> expectedOffsets;
                qint64 sent = offset;
                for (int index = 0; index < window && sent < image.size(); ++index) {
                  const auto bytes = image.mid(sent, kChunkSize);
                  auto chunk = baseRequest(info, fileId);
                  chunk.insert("offset", double(sent));
                  chunk.insert("data", QString::fromLatin1(bytes.toBase64()));
                  if (!sendRequest(socket, 1052, chunk, error, cancelled))
                    break;
                  sent += bytes.size();
                  expectedOffsets.append(sent);
                }
                for (const auto expected : expectedOffsets) {
                  if (!error.isEmpty() || !receiveResponse(socket, 1052, reply, error, cancelled))
                    break;
                  const auto next = reply.value("offset").toVariant().toLongLong();
                  if (next != expected) {
                    error = tr("资源服务确认的上传断点与分块不一致");
                    break;
                  }
                  offset = next;
                }
                if (!error.isEmpty())
                  break;
                if (offset == image.size() || progressTimer.elapsed() >= kProgressIntervalMs) {
                  progressTimer.restart();
                  const auto size = image.size();
                  QMetaObject::invokeMethod(
                      this,
                      [this, requestId, offset, size, generation] {
                        if (ChatStore::instance()->Generation() != generation)
                          return;
                        emit uploadProgress(requestId, offset, size);
                      },
                      Qt::QueuedConnection);
                }
              }
              if (error.isEmpty() && offset == image.size() && !(reply.value("complete").toBool()))
                error = tr("资源服务未确认上传完成");
            }
          }
        }
        QMetaObject::invokeMethod(
            this,
            [this, requestId, fileId, image, error, info, generation, cancelled] {
              if (cancelled->load() || ChatStore::instance()->Generation() != generation)
                return;
              --_uploadTasks;
              if (error.isEmpty() && UserMgr::GetInstance()->GetUid() == info.uid) {
                rememberImage(fileId, image);
                if (ChatStore::instance()->IsReady())
                  ChatStore::instance()->SaveAttachment(fileId, image);
              }
              if (error.isEmpty())
                qInfo() << "Image upload completed:" << "request=" << requestId
                        << "file=" << fileId.left(8) << "bytes=" << image.size();
              else
                qWarning() << "Image upload failed:" << "request=" << requestId
                           << "uid=" << info.uid << "reason=" << error;
              emit uploadFinished(requestId, error.isEmpty() ? fileId : QString(), error);
            },
            Qt::QueuedConnection);
      }));
}

void ResourceMgr::fetchImage(const QString &fileId)
{
  // 缓存及后台传输按登录代数隔离，同一账号重新登录也拒绝旧回调。
  rememberImage({}, {});
  const auto key = cacheKey(UserMgr::GetInstance()->GetUid(), fileId);
  const auto cached = cachedImage(fileId);
  if (!cached.isEmpty()) {
    emit imageReady(fileId, cached, {});
    return;
  }
  if (_fetching.contains(key))
    return;
  _fetching.insert(key);
  if (!ChatStore::instance()->IsReady()) {
    downloadImage(fileId);
    return;
  }
  const auto callback = [this, fileId, key](const ChatStoreResult &result) {
    if (!result.image.isEmpty()) {
      rememberImage(fileId, result.image);
      _fetching.remove(key);
      emit imageReady(fileId, result.image, {});
    } else if (fileId.startsWith("pending:")) {
      _fetching.remove(key);
      emit imageReady(fileId, {}, tr("本机待发送图片未找到"));
    } else
      downloadImage(fileId);
  };
  if (fileId.startsWith("pending:"))
    ChatStore::instance()->LoadPendingImage(fileId.mid(8), callback);
  else
    ChatStore::instance()->LoadAttachment(fileId, callback);
}

void ResourceMgr::downloadImage(const QString &fileId)
{
  const auto info = connectionInfo();
  const auto key = cacheKey(info.uid, fileId);
  const auto generation = ChatStore::instance()->Generation();
  if (_downloadTasks >= kMaxDownloadTasks) {
    _fetching.remove(key);
    QTimer::singleShot(0, this, [this, fileId, generation] {
      if (ChatStore::instance()->Generation() == generation)
        emit imageReady(fileId, {}, tr("下载队列繁忙，请稍后重试"));
    });
    return;
  }
  ++_downloadTasks;
  const auto cancelled = _cancelled;
  const QByteArray partial = _partials.value(key);
  qInfo() << "Image download started:" << "uid=" << info.uid << "file=" << fileId.left(8)
          << "resume_offset=" << partial.size();
  _downloadPool.start(QRunnable::create([this, fileId, info, partial, key, generation, cancelled] {
    if (cancelled->load())
      return;
    QString error;
    QByteArray image = partial;
    QSslSocket socket;
    if (connectSocket(socket, info, error, cancelled)) {
      qint64 offset = image.size();
      qint64 size = -1;
      QString checksum;
      QElapsedTimer progressTimer;
      progressTimer.start();
      do {
        if (cancelled->load())
          return;
        auto request = baseRequest(info, fileId);
        request.insert("offset", double(offset));
        QJsonObject reply;
        if (!exchange(socket, 1054, request, reply, error, cancelled))
          break;
        const auto bytes = QByteArray::fromBase64(reply.value("data").toString().toLatin1());
        const qint64 next = reply.value("offset").toVariant().toLongLong();
        size = reply.value("size").toVariant().toLongLong();
        checksum = reply.value("sha256").toString();
        if (size <= 0 || size > kMaxImageSize || bytes.size() > kChunkSize ||
            next != offset + bytes.size() || next > size ||
            (bytes.isEmpty() && !reply.value("complete").toBool())) {
          error = tr("下载数据无效");
          break;
        }
        image += bytes;
        offset = next;
        if (offset == size || progressTimer.elapsed() >= kProgressIntervalMs) {
          progressTimer.restart();
          QMetaObject::invokeMethod(
              this,
              [this, fileId, offset, size, generation] {
                if (ChatStore::instance()->Generation() == generation)
                  emit downloadProgress(fileId, offset, size);
              },
              Qt::QueuedConnection);
        }
        if (reply.value("complete").toBool())
          break;
      } while (error.isEmpty());
      if (error.isEmpty() &&
          (image.size() != size ||
           QString::fromLatin1(
               QCryptographicHash::hash(image, QCryptographicHash::Sha256).toHex()) != checksum)) {
        image.clear();
        error = tr("图片校验失败");
      }
    }
    QMetaObject::invokeMethod(
        this,
        [this, fileId, image, error, info, key, generation, cancelled] {
          if (cancelled->load() || ChatStore::instance()->Generation() != generation)
            return;
          --_downloadTasks;
          _fetching.remove(key);
          if (UserMgr::GetInstance()->GetUid() != info.uid)
            return;
          if (error.isEmpty()) {
            rememberImage(fileId, image);
            if (ChatStore::instance()->IsReady())
              ChatStore::instance()->SaveAttachment(fileId, image);
            _partials.remove(key);
          } else if (!image.isEmpty()) {
            _partials.insert(key, image);
            qint64 partialBytes = 0;
            for (const auto &bytes : _partials)
              partialBytes += bytes.size();
            while (partialBytes > kPartialCacheLimit && _partials.size() > 1) {
              auto oldest = _partials.begin();
              if (oldest.key() == key)
                ++oldest;
              partialBytes -= oldest.value().size();
              _partials.erase(oldest);
            }
          } else
            _partials.remove(key);
          if (error.isEmpty())
            qInfo() << "Image download completed:" << "uid=" << info.uid
                    << "file=" << fileId.left(8) << "bytes=" << image.size();
          else
            qWarning() << "Image download failed:" << "uid=" << info.uid
                       << "file=" << fileId.left(8) << "reason=" << error;
          emit imageReady(fileId, image, error);
        },
        Qt::QueuedConnection);
  }));
}
