#include "mobileupdateservice.h"
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QVersionNumber>
#include <QtConcurrent/QtConcurrentRun>

MobileUpdateService::MobileUpdateService(const QUrl &origin, const QString &version,
                                         QObject *parent,
                                         std::shared_ptr<MobileDownloadBackend> backend,
                                         QNetworkAccessManager *network)
    : QObject(parent), _checker(origin, version, this, network, true), _backend(std::move(backend)),
      _origin(origin), _version(version)
{
  connect(&_checker, &ClientUpdateService::stateChanged, this, [this](State state) {
    if (!_backend)
      emit stateChanged(state);
  });
  connect(&_checker, &ClientUpdateService::checkFinished, this, [this](bool available) {
    if (_backend) {
      const bool cached =
          _beforeCheck == State::Ready && available && _info.version == _checker.info().version &&
          _info.sha256 == _checker.info().sha256 && _info.size == _checker.info().size;
      _info = _checker.info();
      if (!cached)
        _received = _sampleBytes = _speed = 0;
      setState(cached ? State::Ready : available ? State::Available : State::Idle);
    }
    emit checkFinished(available);
  });
  connect(&_checker, &ClientUpdateService::failed, this, [this](const QString &error) {
    if (_backend)
      setState(_beforeCheck == State::Ready ? State::Ready
               : _info.version.isEmpty()    ? State::Idle
                                            : State::Available);
    emit failed(error);
  });
  connect(&_checker, &ClientUpdateService::downloadProgress, this,
          &MobileUpdateService::downloadProgress);
  connect(&_checker, &ClientUpdateService::downloadReady, this,
          &MobileUpdateService::downloadReady);
  _poll.setInterval(1000);
  connect(&_poll, &QTimer::timeout, this, &MobileUpdateService::refresh);
  if (_backend)
    QTimer::singleShot(0, this, &MobileUpdateService::restore);
}

MobileUpdateService::State MobileUpdateService::state() const
{
  return _backend ? _state : _checker.state();
}
const ClientUpdateInfo &MobileUpdateService::info() const
{
  return _backend ? _info : _checker.info();
}
QString MobileUpdateService::releaseNotes() const
{
  return _backend ? _info.notes : _checker.releaseNotes();
}
qint64 MobileUpdateService::downloadedBytes() const
{
  return _backend ? _received : _checker.downloadedBytes();
}
qint64 MobileUpdateService::bytesPerSecond() const
{
  return _backend ? _speed : _checker.bytesPerSecond();
}

void MobileUpdateService::setState(State state)
{
  if (_state == state)
    return;
  _state = state;
  emit stateChanged(state);
}
void MobileUpdateService::setStatus(const QString &status)
{
  if (_status == status)
    return;
  _status = status;
  emit statusChanged();
}
void MobileUpdateService::report(const QString &error)
{
  _poll.stop();
  _speed = 0;
  setState(State::Available);
  setStatus(error);
  emit failed(error);
}
QJsonObject MobileUpdateService::manifest() const
{
  return {{"version", _info.version},
          {"notes", _info.notes},
          {"downloadUrl", _info.downloadUrl.toString()},
          {"sha256", QString::fromLatin1(_info.sha256)},
          {"size", _info.size}};
}
void MobileUpdateService::restore()
{
  const auto record = _backend->inspect();
  if (!record.contains("manifest")) {
    if (record.contains("error"))
      setStatus(record["error"].toString());
    return;
  }
  QString error;
  if (!ClientUpdateService::parseManifest(QJsonDocument(record["manifest"].toObject()).toJson(),
                                          _origin, _info, error, true) ||
      QVersionNumber::fromString(_info.version) <= QVersionNumber::fromString(_version)) {
    _backend->cancel();
    _info = {};
    return;
  }
  setState(State::Downloading);
  _poll.start();
  refresh();
}
void MobileUpdateService::refresh()
{
  if (!_backend || _verifying || _state != State::Downloading)
    return;
  const auto record = _backend->inspect();
  if (record.contains("error")) {
    report(record["error"].toString());
    return;
  }
  _received = qBound(qint64(0), record["received"].toVariant().toLongLong(), _info.size);
  if (_rateTimer.isValid() && _rateTimer.elapsed() > 0)
    _speed = qMax(qint64(0), _received - _sampleBytes) * 1000 / _rateTimer.elapsed();
  _sampleBytes = _received;
  _rateTimer.start();
  emit downloadProgress(_received, _info.size);
  switch (record["status"].toInt()) {
  case 1:
    setStatus(tr("等待安卓系统开始下载，可锁屏或离开应用"));
    break;
  case 2:
    setStatus(tr("系统正在后台下载，可锁屏或离开应用"));
    break;
  case 4:
    _speed = 0;
    setStatus(tr("系统暂缓下载，等待网络或系统条件恢复（%1）").arg(record["reason"].toInt()));
    break;
  case 8:
    validate(false);
    break;
  default:
    report(tr("系统下载失败或任务被移除（%1），可重试").arg(record["reason"].toInt()));
  }
}
void MobileUpdateService::check()
{
  if (_backend && (_verifying || _state == State::Checking || _state == State::Downloading))
    return;
  setStatus({});
  if (_backend) {
    _beforeCheck = _state;
    setState(State::Checking);
  }
  _checker.check();
}
void MobileUpdateService::download()
{
  if (!_backend) {
    _checker.download();
    return;
  }
  if (_state != State::Available || _verifying)
    return;
  const auto record = _backend->start(manifest());
  if (record.contains("error")) {
    report(record["error"].toString());
    return;
  }
  _received = _sampleBytes = _speed = 0;
  _rateTimer.invalidate();
  setState(State::Downloading);
  _poll.start();
  refresh();
}
void MobileUpdateService::cancel()
{
  if (!_backend) {
    _checker.cancel();
    return;
  }
  if (_verifying)
    return;
  const auto result = _backend->cancel();
  if (result.contains("error")) {
    setStatus(result["error"].toString());
    return;
  }
  _poll.stop();
  _received = _sampleBytes = _speed = 0;
  setState(State::Available);
  setStatus(tr("已取消下载；再次下载会创建新任务"));
  emit downloadProgress(0, _info.size);
}
void MobileUpdateService::validate(bool install)
{
  _verifying = true;
  _speed = 0;
  setStatus(tr("正在校验完整安装包及应用签名"));
  emit statusChanged();
  auto watcher = new QFutureWatcher<QJsonObject>(this);
  connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, install] {
    const auto result = watcher->result();
    watcher->deleteLater();
    _verifying = false;
    emit statusChanged();
    if (result.contains("error")) {
      report(result["error"].toString());
      return;
    }
    _poll.stop();
    _received = _info.size;
    setState(State::Ready);
    setStatus(tr("安装包已保存并校验，可以安装；退出后仍会保留"));
    emit downloadReady(result["path"].toString());
    if (install && QGuiApplication::applicationState() == Qt::ApplicationActive) {
      const auto launched = _backend->install();
      if (launched.contains("error")) {
        setStatus(launched["error"].toString());
        emit failed(launched["error"].toString());
      } else if (launched.contains("message")) {
        setStatus(launched["message"].toString());
      }
    }
  });
  const auto backend = _backend;
  watcher->setFuture(QtConcurrent::run([backend] { return backend->verify(); }));
}
bool MobileUpdateService::launchInstaller(const QString &directory, QString &error)
{
  if (!_backend)
    return _checker.launchInstaller(directory, error);
  if (_state != State::Ready || _verifying) {
    error = tr("请等待安装包完成校验");
    return false;
  }
  validate(true);
  return true;
}
