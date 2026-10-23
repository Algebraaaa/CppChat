#pragma once

#include <QCryptographicHash>
#include <QMap>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <memory>

class QSaveFile;
class QTemporaryDir;
class QNetworkReply;

struct ClientUpdateInfo {
  QString version;
  QString notes;
  QMap<QString, QString> history;
  QUrl downloadUrl;
  QByteArray sha256;
  qint64 size = 0;
};

// Owns update transport and verified files; UI and application shutdown stay with the caller.
class ClientUpdateService : public QObject {
  Q_OBJECT
public:
  enum class State { Idle, Checking, Available, Downloading, Ready };
  Q_ENUM(State)
  explicit ClientUpdateService(const QUrl &manifestUrl, const QString &currentVersion,
                               QObject *parent = nullptr, QNetworkAccessManager *network = nullptr,
                               bool android = false);
  ~ClientUpdateService() override;
  State state() const { return _state; }
  bool busy() const { return _state == State::Checking || _state == State::Downloading; }
  const ClientUpdateInfo &info() const { return _info; }
  QString currentVersion() const { return _currentVersion; }
  QString releaseNotes() const;
  void check();
  void download();
  void cancel();
  // Call only after the main window has destroyed its sessions and SQLite connections.
  bool launchInstaller(const QString &installationDirectory, QString &error);
  static bool parseManifest(const QByteArray &json, const QUrl &manifestUrl, ClientUpdateInfo &info,
                            QString &error, bool android = false);
  static QUrl manifestUrlForServer(const QUrl &serverUrl, bool android = false);
  static QStringList installerArguments(const QString &installationDirectory);

signals:
  void stateChanged(ClientUpdateService::State state);
  void checkFinished(bool available);
  void downloadProgress(qint64 received, qint64 total);
  void downloadReady(const QString &path);
  void failed(const QString &message);

private:
  void setState(State state);
  void stopReply();
  void fail(const QString &message);
  bool responseAccepted() const;
  void readDownload();
  QUrl _manifestUrl;
  QString _currentVersion;
  QNetworkAccessManager *_network;
  QPointer<QNetworkReply> _reply;
  QTimer _deadline;
  State _state = State::Idle;
  ClientUpdateInfo _info;
  QByteArray _manifest;
  std::unique_ptr<QTemporaryDir> _directory;
  std::unique_ptr<QSaveFile> _file;
  QCryptographicHash _hash{QCryptographicHash::Sha256};
  qint64 _received = 0;
  QString _readyPath;
  bool _android = false;
};
