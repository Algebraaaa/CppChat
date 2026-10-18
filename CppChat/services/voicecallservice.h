#pragma once
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

// Transport-independent call state. Owns neither widgets nor audio devices.
class VoiceCallService : public QObject {
  Q_OBJECT
public:
  enum class State { Idle, Outgoing, Incoming, Connecting, Active };
  Q_ENUM(State)
  explicit VoiceCallService(QObject *parent = nullptr);
  State state() const { return state_; }
  int peer() const { return peer_; }
  QString callId() const { return call_; }
  bool start(int peer, bool video = false);
  bool isVideo() const { return video_; }
  void setCameraEnabled(bool enabled);
  void sendVideo(const QByteArray &jpeg);
  void receiveVideo(const QJsonObject &event);
  void accept();
  void finish(const QString &reason = {});
  void disconnected();
  void receive(const QJsonObject &event);
  void receiveAudio(const QJsonObject &event);
  void sendAudio(const QByteArray &opus);
signals:
  void send(QJsonObject body, bool audio);
  void changed();
  void ended(QString reason);
  void audio(QByteArray opus, int sequence);
  void sendVideoFrame(QJsonObject body);
  void videoFrame(QByteArray jpeg);
  void peerCameraChanged(bool enabled);

private:
  void command(const QString &action);
  void reset(const QString &reason);
  State state_ = State::Idle;
  int peer_ = 0;
  QString call_;
  int sequence_ = 0;
  int received_ = 0;
  int videoSequence_ = 0, receivedVideo_ = 0;
  bool video_ = false, cameraEnabled_ = true, peerCameraEnabled_ = true;
  QTimer deadline_, keepalive_;
  QElapsedTimer lastAck_;
};
