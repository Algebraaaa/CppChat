#pragma once
#include <QBuffer>
#include <QObject>
#include <QTimer>
class QAudioSource;
class QAudioSink;
class QIODevice;

// One GUI-thread device owner for voice messages and their favorite previews.
// Real-time calls preempt this owner before opening their own devices.
class VoiceMessageAudio : public QObject {
  Q_OBJECT
public:
  static VoiceMessageAudio *instance();
  ~VoiceMessageAudio() override;
  bool StartRecording();
  void StopRecording();
  bool Play(const QByteArray &wave, const QString &key);
  void Cancel();
  void SetCallBusy(bool busy);
  bool CallBusy() const { return _callBusy; }
  bool Recording() const { return _input != nullptr; }
  QString PlayingKey() const { return _playing; }
  static int DurationMs(const QByteArray &wave);
  static QByteArray FromPcm(const QByteArray &pcm);
signals:
  void recorded(QByteArray wave);
  void recordingProgress(int milliseconds);
  void changed();
  void failed(QString message);

private:
  explicit VoiceMessageAudio(QObject *parent);
  void Capture();
  void StopDevices();
  QAudioSource *_input = nullptr;
  QAudioSink *_output = nullptr;
  QIODevice *_capture = nullptr;
  QBuffer _playback;
  QByteArray _pcm;
  QString _playing;
  QTimer _limit, _progress;
  quint64 _revision = 0;
  bool _callBusy = false;
};
