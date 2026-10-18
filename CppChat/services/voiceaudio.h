#pragma once
#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QTimer>
#include <memory>
class QAudioSource;
class QAudioSink;
class QIODevice;
struct OpusEncoder;
struct OpusDecoder;

// Fixed 20 ms Opus frames; bounded capture/playout queues discard stale audio.
class VoiceAudio : public QObject {
  Q_OBJECT
public:
  explicit VoiceAudio(QObject *parent = nullptr);
  ~VoiceAudio() override;
  bool start(QString &error);
  void stop();
  void setMuted(bool muted) { muted_ = muted; }
  void receive(const QByteArray &opus, int sequence);
  static QByteArray encodeFrame(OpusEncoder *encoder, const QByteArray &pcm);
  static QByteArray decodeFrame(OpusDecoder *decoder, const QByteArray &packet);
signals:
  void encoded(QByteArray opus);
  void failed(QString error);

private:
  void capture();
  void play();
  QAudioSource *input_ = nullptr;
  QAudioSink *output_ = nullptr;
  QIODevice *capture_ = nullptr;
  QIODevice *playback_ = nullptr;
  QAudioFormat inputFormat_, outputFormat_;
  OpusEncoder *encoder_ = nullptr;
  OpusDecoder *decoder_ = nullptr;
  QByteArray captureBuffer_, playbackBuffer_;
  QQueue<QByteArray> packets_;
  QTimer playout_;
  int lastSequence_ = 0;
  bool muted_ = false;
  bool primed_ = false;
  quint64 generation_ = 0;
};
