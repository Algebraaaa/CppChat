#include "services/clientupdateservice.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVersionNumber>
#include <algorithm>
#include <cmath>

namespace {
constexpr qint64 kManifestLimit = 64 * 1024;
constexpr qint64 kInstallerLimit = 512 * 1024 * 1024;
bool secureUrl(const QUrl &url)
{
  return url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty() &&
         url.userInfo().isEmpty() && !url.hasFragment() && !url.hasQuery();
}
bool sameOrigin(const QUrl &left, const QUrl &right)
{
  return secureUrl(left) && secureUrl(right) && left.host() == right.host() &&
         left.port(443) == right.port(443);
}
bool validVersion(const QString &version)
{
  static const QRegularExpression pattern(
      QStringLiteral("^(0|[1-9][0-9]{0,4})\\.(0|[1-9][0-9]{0,4})\\.(0|[1-9][0-9]{0,4})$"));
  return pattern.match(version).hasMatch();
}
QNetworkRequest updateRequest(const QUrl &url)
{
  QNetworkRequest request(url);
  // Update URLs are immutable and must stay on the configured HTTPS origin.
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  request.setTransferTimeout(20000);
  return request;
}
} // namespace

ClientUpdateService::ClientUpdateService(const QUrl &manifestUrl, const QString &currentVersion,
                                         QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), _manifestUrl(manifestUrl), _currentVersion(currentVersion),
      _network(network ? network : new QNetworkAccessManager(this))
{
  _deadline.setSingleShot(true);
  connect(&_deadline, &QTimer::timeout, this, [this]() { fail(tr("检查或下载更新超时，请重试")); });
}

ClientUpdateService::~ClientUpdateService() { stopReply(); }

QString ClientUpdateService::releaseNotes() const
{
  if (!validVersion(_currentVersion) || !validVersion(_info.version))
    return {};
  const auto current = QVersionNumber::fromString(_currentVersion);
  const auto latest = QVersionNumber::fromString(_info.version);
  if (latest <= current)
    return {};
  auto entries = _info.history;
  if (_info.history.isEmpty() && !_info.version.isEmpty() && !_info.notes.isEmpty())
    entries.insert(_info.version, _info.notes);
  auto versions = entries.keys();
  std::sort(versions.begin(), versions.end(), [](const auto &a, const auto &b) {
    return QVersionNumber::fromString(a) > QVersionNumber::fromString(b);
  });
  QStringList notes;
  for (const auto &version : versions) {
    const auto number = QVersionNumber::fromString(version);
    if (number > current && number <= latest)
      notes.append(QStringLiteral("v%1\n%2").arg(version, entries.value(version)));
  }
  return notes.join(QStringLiteral("\n\n"));
}

QUrl ClientUpdateService::manifestUrlForServer(const QUrl &serverUrl)
{
  return secureUrl(serverUrl)
             ? serverUrl.resolved(QUrl(QStringLiteral("/updates/windows/latest.json")))
             : QUrl();
}

bool ClientUpdateService::parseManifest(const QByteArray &json, const QUrl &manifestUrl,
                                        ClientUpdateInfo &info, QString &error)
{
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(json, &parseError);
  if (json.size() > kManifestLimit || parseError.error != QJsonParseError::NoError ||
      !document.isObject() || !secureUrl(manifestUrl)) {
    error = tr("更新信息格式错误");
    return false;
  }
  const auto object = document.object();
  ClientUpdateInfo candidate;
  candidate.version = object.value("version").toString();
  candidate.notes = object.value("notes").toString();
  const QString download = object.value("downloadUrl").toString();
  candidate.downloadUrl = manifestUrl.resolved(QUrl(download));
  candidate.sha256 = object.value("sha256").toString().toLatin1().toLower();
  const double size = object.value("size").toDouble(-1);
  static const QRegularExpression hashPattern(QStringLiteral("^[0-9a-f]{64}$"));
  if (!validVersion(candidate.version) || candidate.notes.size() > 8000 || download.isEmpty() ||
      !sameOrigin(manifestUrl, candidate.downloadUrl) ||
      !candidate.downloadUrl.path().endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) ||
      !hashPattern.match(QString::fromLatin1(candidate.sha256)).hasMatch() || size < 1 ||
      size > kInstallerLimit || std::floor(size) != size) {
    error = tr("更新信息不完整或下载地址不可信");
    return false;
  }
  candidate.size = static_cast<qint64>(size);
  if (object.contains("history")) {
    const auto history = object["history"].toArray();
    if (!object["history"].isArray() || history.size() > 200) {
      error = tr("更新历史格式错误");
      return false;
    }
    for (const auto &entry : history) {
      const auto item = entry.toObject();
      const auto version = item["version"].toString();
      const auto notes = item["notes"].toString();
      if (!validVersion(version) || notes.trimmed().isEmpty() || notes.size() > 8000 ||
          candidate.history.contains(version) ||
          QVersionNumber::fromString(version) > QVersionNumber::fromString(candidate.version)) {
        error = tr("更新历史格式错误");
        return false;
      }
      candidate.history.insert(version, notes);
    }
    if (!candidate.history.contains(candidate.version)) {
      error = tr("更新历史缺少当前版本");
      return false;
    }
  }
  info = candidate;
  error.clear();
  return true;
}

void ClientUpdateService::setState(State state)
{
  _state = state;
  emit stateChanged(state);
}

void ClientUpdateService::stopReply()
{
  _deadline.stop();
  if (_reply) {
    disconnect(_reply, nullptr, this, nullptr);
    _reply->abort();
    _reply->deleteLater();
    _reply = nullptr;
  }
}

void ClientUpdateService::fail(const QString &message)
{
  const bool downloading = _state == State::Downloading;
  stopReply();
  _file.reset(); // QSaveFile removes its incomplete file.
  _directory.reset();
  _readyPath.clear();
  setState(downloading ? State::Available : State::Idle);
  emit failed(message);
}

bool ClientUpdateService::responseAccepted() const
{
  return _reply && _reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
         sameOrigin(_manifestUrl, _reply->url());
}

void ClientUpdateService::check()
{
  if (busy())
    return;
  _directory.reset();
  _readyPath.clear();
  _info = {};
  _manifest.clear();
  if (!secureUrl(_manifestUrl) || !validVersion(_currentVersion)) {
    fail(tr("当前服务器未配置 HTTPS 更新入口"));
    return;
  }
  setState(State::Checking);
  _reply = _network->get(updateRequest(_manifestUrl));
  _reply->setReadBufferSize(kManifestLimit + 1);
  _deadline.start(15000);
  connect(_reply, &QNetworkReply::readyRead, this, [this]() {
    _manifest += _reply->readAll();
    if (_manifest.size() > kManifestLimit)
      fail(tr("更新信息超过大小限制"));
  });
  connect(_reply, &QNetworkReply::finished, this, [this]() {
    _manifest += _reply->readAll();
    if (_reply->error() != QNetworkReply::NoError || !responseAccepted()) {
      fail(tr("无法获取更新信息，请稍后重试"));
      return;
    }
    QString error;
    if (!parseManifest(_manifest, _manifestUrl, _info, error)) {
      fail(error);
      return;
    }
    stopReply();
    const bool available = QVersionNumber::compare(QVersionNumber::fromString(_info.version),
                                                   QVersionNumber::fromString(_currentVersion)) > 0;
    setState(available ? State::Available : State::Idle);
    emit checkFinished(available);
  });
}

void ClientUpdateService::download()
{
  if (_state != State::Available)
    return;
  const QString cache =
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/updates";
  if (!QDir().mkpath(cache)) {
    fail(tr("无法创建更新下载目录"));
    return;
  }
  _directory = std::make_unique<QTemporaryDir>(cache + "/download-XXXXXX");
  _readyPath = _directory->filePath("CppChat-Setup-" + _info.version + "-x64.exe");
  _file = std::make_unique<QSaveFile>(_readyPath);
  if (!_directory->isValid() || !_file->open(QIODevice::WriteOnly)) {
    fail(tr("无法保存更新安装包"));
    return;
  }
  _received = 0;
  _hash.reset();
  setState(State::Downloading);
  _reply = _network->get(updateRequest(_info.downloadUrl));
  _reply->setReadBufferSize(256 * 1024);
  _deadline.start(10 * 60 * 1000);
  connect(_reply, &QNetworkReply::readyRead, this, &ClientUpdateService::readDownload);
  connect(_reply, &QNetworkReply::finished, this, [this]() {
    readDownload();
    if (!_reply)
      return;
    if (_reply->error() != QNetworkReply::NoError || !responseAccepted()) {
      fail(tr("下载更新失败，请重试"));
      return;
    }
    if (_received != _info.size || _hash.result().toHex() != _info.sha256) {
      fail(tr("安装包校验失败，请重新下载"));
      return;
    }
    if (!_file->commit()) {
      fail(tr("无法保存更新安装包"));
      return;
    }
    _file.reset();
    stopReply();
    setState(State::Ready);
    emit downloadReady(_readyPath);
  });
}

void ClientUpdateService::readDownload()
{
  if (!_reply || !_file)
    return;
  if (!responseAccepted()) {
    fail(tr("更新下载响应无效"));
    return;
  }
  while (_reply && _file && _reply->bytesAvailable() > 0) {
    const QByteArray chunk = _reply->read(64 * 1024);
    if (chunk.isEmpty())
      break;
    _received += chunk.size();
    if (_received > _info.size || _file->write(chunk) != chunk.size()) {
      fail(tr("安装包大小不符或磁盘空间不足"));
      return;
    }
    _hash.addData(chunk);
    emit downloadProgress(_received, _info.size);
  }
}

void ClientUpdateService::cancel()
{
  const bool downloading = _state == State::Downloading;
  stopReply();
  _file.reset();
  _directory.reset();
  _readyPath.clear();
  setState(downloading || _state == State::Ready ? State::Available : State::Idle);
}

QStringList ClientUpdateService::installerArguments(const QString &installationDirectory)
{
  return {QStringLiteral("/DIR=") + QDir::toNativeSeparators(installationDirectory),
          QStringLiteral("/NORESTART"), QStringLiteral("/SP-")};
}

bool ClientUpdateService::launchInstaller(const QString &installationDirectory, QString &error)
{
#ifdef Q_OS_WIN
  QFile installer(_readyPath);
  if (_state != State::Ready || !installer.open(QIODevice::ReadOnly) ||
      installer.size() != _info.size) {
    error = tr("已下载的安装包不可用，请重新下载");
    return false;
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&installer) || hash.result().toHex() != _info.sha256) {
    error = tr("已下载的安装包发生变化，请重新下载");
    return false;
  }
  installer.close();
  if (!QProcess::startDetached(_readyPath, installerArguments(installationDirectory),
                               installationDirectory)) {
    error = tr("无法启动更新安装程序");
    return false;
  }
  // The installer runs beyond this process. Retain only a successfully handed-off download.
  _directory->setAutoRemove(false);
  return true;
#else
  Q_UNUSED(installationDirectory);
  error = tr("此更新安装包仅支持 Windows");
  return false;
#endif
}
