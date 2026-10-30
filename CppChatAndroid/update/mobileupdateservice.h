#pragma once

#include "downloadbackend.h"
#include "services/clientupdateservice.h"

// Manifest policy is shared; Android APK transfers belong to the system download service.
class MobileUpdateService : public QObject
{
  Q_OBJECT
public:
  using State = ClientUpdateService::State;
  MobileUpdateService(const QUrl &origin, const QString &version, QObject *parent = nullptr,
                      std::shared_ptr<MobileDownloadBackend> backend = makeAndroidDownloadBackend(),
                      QNetworkAccessManager *network = nullptr);
  State state() const;
  const ClientUpdateInfo &info() const;
  QString releaseNotes() const;
  QString statusText() const
  {
    return _status;
  }
  qint64 downloadedBytes() const;
  qint64 bytesPerSecond() const;
  bool backgroundManaged() const
  {
    return bool(_backend);
  }
  bool verifying() const
  {
    return _verifying;
  }
  void check();
  void download();
  void cancel();
  void refresh();
  bool launchInstaller(const QString &directory, QString &error);

signals:
  void stateChanged(ClientUpdateService::State state);
  void checkFinished(bool available);
  void downloadProgress(qint64 received, qint64 total);
  void downloadReady(const QString &path);
  void failed(const QString &message);
  void statusChanged();

private:
  void restore();
  void setState(State state);
  void setStatus(const QString &status);
  void validate(bool install);
  void report(const QString &error);
  QJsonObject manifest() const;
  ClientUpdateService _checker;
  std::shared_ptr<MobileDownloadBackend> _backend;
  QUrl _origin;
  QString _version, _status;
  ClientUpdateInfo _info;
  State _state = State::Idle;
  State _beforeCheck = State::Idle;
  QTimer _poll;
  QElapsedTimer _rateTimer;
  qint64 _received = 0, _sampleBytes = 0, _speed = 0;
  bool _verifying = false;
};
