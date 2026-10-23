#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QVideoSink>
#include <functional>
class VoiceCallService;
class VoiceAudio;
class VideoMedia;

// Mobile device/permission coordinator; existing services own codecs and protocol.
class MobileMedia : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QVariantMap call READ call NOTIFY changed)
  Q_PROPERTY(bool permissionPending READ permissionPending NOTIFY changed)
  Q_PROPERTY(bool recording READ recording NOTIFY changed)
  Q_PROPERTY(int recordingMs READ recordingMs NOTIFY changed)
  Q_PROPERTY(int draftMs READ draftMs NOTIFY changed)
  Q_PROPERTY(int recordingThread READ recordingThread NOTIFY changed)
  Q_PROPERTY(QString playing READ playing NOTIFY changed)
public:
  explicit MobileMedia(QObject *parent = nullptr);
  ~MobileMedia() override;
  QVariantMap call() const;
  bool permissionPending() const { return m_permissionPending; }
  bool recording() const;
  int recordingMs() const { return m_recordingMs; }
  int draftMs() const;
  int recordingThread() const { return m_recordingThread; }
  QString playing() const;
  QByteArray draft() const { return m_draft; }
  bool busy() const;
  void startCall(int peer, bool video);
  void startRecording(int thread);
  void reset();
  Q_INVOKABLE void acceptCall();
  Q_INVOKABLE void endCall();
  Q_INVOKABLE void setMuted(bool muted);
  Q_INVOKABLE void setCameraEnabled(bool enabled);
  Q_INVOKABLE void switchCamera();
  Q_INVOKABLE void setVideoSinks(QVideoSink *local, QVideoSink *remote);
  Q_INVOKABLE void finishRecording();
  Q_INVOKABLE void discardRecording();
  Q_INVOKABLE void leaveChat(int thread);
  Q_INVOKABLE void playVoice(const QString &fileId);
  void clearDraft();
signals:
  void changed();
  void failed(QString message);
  void status(QString message);
private:
  void requestPermissions(bool video, std::function<void()> action);
  void checkPermissions(bool video, quint64 revision, std::function<void()> action);
  void refreshCall();
  void voiceReady(const QString &id, const QByteArray &bytes, const QString &error);
  VoiceCallService *m_call;
  VoiceAudio *m_audio;
  VideoMedia *m_video;
  QPointer<QVideoSink> m_local, m_remote;
  QElapsedTimer m_duration;
  QTimer m_tick;
  QByteArray m_draft;
  QString m_loadingVoice;
  quint64 m_permissionRevision = 0, m_voiceGeneration = 0;
  int m_recordingThread = 0, m_recordingMs = 0;
  bool m_permissionPending = false, m_started = false, m_muted = false;
  bool m_camera = true, m_peerCamera = true, m_front = true;
};
