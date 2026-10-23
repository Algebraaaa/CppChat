#include "voicecallservice.h"
#include "media/callpolicy.h"
#include <QDebug>
#include <QRegularExpression>
#include <QUuid>
#include <utility>

namespace {
QString failure(int code)
{
  switch (code) {
  case 1021:
    return QObject::tr("对方正在通话");
  case 1022:
    return QObject::tr("对方不在线或连接已断开");
  case 1017:
    return QObject::tr("只有好友之间可以通话");
  case 1025:
    return QObject::tr("呼叫过于频繁，请稍后重试");
  default:
    return QObject::tr("通话连接已失效，请重新呼叫");
  }
}
} // namespace
VoiceCallService::VoiceCallService(QObject *parent, Clock clock) : QObject(parent)
{
  monotonic_.start();
  now_ = clock ? std::move(clock) : Clock([this] { return monotonic_.elapsed(); });
  deadline_.setSingleShot(true);
  connect(&deadline_, &QTimer::timeout, this, [this] { finish(tr("对方未接听或网络超时")); });
  keepalive_.setInterval(cppchat::media::CallPolicy::HeartbeatMs);
  connect(&keepalive_, &QTimer::timeout, this, [this] {
    const auto elapsed = now_() - lastProgress_;
    if (elapsed >= cppchat::media::CallPolicy::TimeoutMs) {
      finish(tr("网络中断，通话已结束"));
      return;
    }
    if (elapsed >= cppchat::media::CallPolicy::RecoveringMs)
      setRecovering(true);
    command("ping"); // Keep probing during a bounded interruption.
  });
}
void VoiceCallService::setRecovering(bool recovering)
{
  if (recovering_ == recovering)
    return;
  recovering_ = recovering;
  qInfo() << "Call network recovery:" << recovering;
  emit networkChanged(recovering);
}
void VoiceCallService::progress(bool recovering)
{
  lastProgress_ = now_();
  setRecovering(recovering);
}
bool VoiceCallService::start(int peer, bool video)
{
  if (state_ != State::Idle || peer <= 0)
    return false;
  peer_ = peer;
  video_ = video;
  call_ = QUuid::createUuid().toString(QUuid::Id128);
  state_ = State::Outgoing;
  deadline_.start(35000);
  emit changed();
  command("invite");
  return true;
}
void VoiceCallService::command(const QString &action)
{
  if (state_ != State::Idle)
    emit send(
        QJsonObject{{"call_id", call_}, {"peer", peer_}, {"action", action}, {"video", video_}},
        false);
}
void VoiceCallService::accept()
{
  if (state_ != State::Incoming)
    return;
  state_ = State::Connecting;
  deadline_.start(10000);
  emit changed();
  command("accept");
}
void VoiceCallService::finish(const QString &reason)
{
  if (state_ == State::Idle)
    return;
  const auto action = state_ == State::Incoming   ? "reject"
                      : state_ == State::Outgoing ? "cancel"
                                                  : "end";
  const QJsonObject body{
      {"call_id", call_}, {"peer", peer_}, {"action", action}, {"video", video_}};
  reset(reason.isEmpty() ? tr("通话已结束") : reason);
  emit send(body, false);
}
void VoiceCallService::reset(const QString &reason)
{
  deadline_.stop();
  keepalive_.stop();
  state_ = State::Idle;
  peer_ = sequence_ = received_ = 0;
  call_.clear();
  videoSequence_ = receivedVideo_ = 0;
  video_ = false;
  cameraEnabled_ = peerCameraEnabled_ = true;
  setRecovering(false);
  qInfo() << "Call ended:" << reason;
  emit changed();
  emit ended(reason);
}
void VoiceCallService::disconnected()
{
  if (state_ != State::Idle)
    reset(tr("聊天连接断开，通话已结束"));
}
void VoiceCallService::receive(const QJsonObject &event)
{
  const auto call = event["call_id"].toString();
  const auto action = event["action"].toString();
  const int peer = event["peer"].toInt();
  static const QRegularExpression id("^[a-f0-9]{32}$");
  if (!id.match(call).hasMatch() || peer <= 0 || !event["error"].isDouble())
    return;
  if (state_ == State::Idle && event["error"].toInt() == 0 && action == "invite" &&
      event["incoming"].toBool()) {
    call_ = call;
    peer_ = peer;
    video_ = event["video"].toBool();
    state_ = State::Incoming;
    deadline_.start(35000);
    emit changed();
    return;
  }
  if (call != call_ || peer != peer_ || state_ == State::Idle)
    return;
  if (event["error"].toInt() != 0) {
    const auto code = event["error"].toInt();
    const bool media = action == "frame" || action == "video";
    const bool temporary =
        code == 1002 || code == 1014 || code == 1015 || code == 1024 || code == 1025;
    if (state_ == State::Active && !event["incoming"].toBool() &&
        ((media && (code == 1023 || temporary)) ||
         ((action == "ping" || action == "camera") && temporary)))
      return; // Drop/retry transient work, without renewing the liveness budget.
    // Best effort teardown; the server lease also expires on failure.
    qWarning() << "Call control rejected:" << action << code;
    finish(failure(event["error"].toInt()));
    return;
  }
  if (event["video"].toBool() != video_) {
    finish(tr("对方客户端不支持本次通话类型"));
    return;
  }
  if (action == "camera" && state_ == State::Active && video_ && event["incoming"].toBool() &&
      event["enabled"].isBool()) {
    peerCameraEnabled_ = event["enabled"].toBool();
    progress();
    emit peerCameraChanged(peerCameraEnabled_);
  } else if (action == "accept" && (state_ == State::Outgoing || state_ == State::Connecting)) {
    deadline_.stop();
    state_ = State::Active;
    progress();
    keepalive_.start();
    emit changed();
  } else if (action == "ping" && state_ == State::Active) {
    progress(event["peer_idle_ms"].toDouble() >= cppchat::media::CallPolicy::RecoveringMs);
    if (video_ && event["peer_camera_enabled"].isBool() &&
        peerCameraEnabled_ != event["peer_camera_enabled"].toBool()) {
      peerCameraEnabled_ = event["peer_camera_enabled"].toBool();
      emit peerCameraChanged(peerCameraEnabled_);
    }
  } else if (action == "reject" || action == "cancel" || action == "end") {
    reset(action == "reject"   ? tr("对方已拒接")
          : action == "cancel" ? tr("对方已取消呼叫")
                               : tr("对方已挂断"));
  }
}
void VoiceCallService::sendAudio(const QByteArray &opus)
{
  if (state_ != State::Active || opus.isEmpty() || opus.size() > 512 || sequence_ >= 100000000)
    return;
  emit send(QJsonObject{{"call_id", call_},
                        {"peer", peer_},
                        {"seq", ++sequence_},
                        {"video", video_},
                        {"audio", QString::fromLatin1(opus.toBase64())}},
            true);
}
void VoiceCallService::receiveAudio(const QJsonObject &event)
{
  if (state_ != State::Active || event["call_id"].toString() != call_ ||
      event["peer"].toInt() != peer_ || !event["seq"].isDouble())
    return;
  const int sequence = event["seq"].toInt();
  const auto encoded = event["audio"].toString().toLatin1();
  if (sequence <= received_ || encoded.isEmpty() || encoded.size() > 684)
    return;
  const auto decoded =
      QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
  if (!decoded || decoded.decoded.isEmpty() || decoded.decoded.size() > 512)
    return;
  received_ = sequence;
  progress();
  emit audio(decoded.decoded, sequence);
}
void VoiceCallService::setCameraEnabled(bool enabled)
{
  if (state_ != State::Active || !video_)
    return;
  cameraEnabled_ = enabled;
  emit send(QJsonObject{{"call_id", call_},
                        {"peer", peer_},
                        {"action", "camera"},
                        {"video", true},
                        {"enabled", enabled}},
            false);
}
void VoiceCallService::sendVideo(const QByteArray &jpeg)
{
  if (state_ != State::Active || !video_ || !cameraEnabled_ || recovering_ || jpeg.isEmpty() ||
      jpeg.size() > 6000 || videoSequence_ >= 100000000)
    return;
  emit sendVideoFrame(QJsonObject{{"call_id", call_},
                                  {"peer", peer_},
                                  {"video", true},
                                  {"seq", ++videoSequence_},
                                  {"image", QString::fromLatin1(jpeg.toBase64())}});
}
void VoiceCallService::receiveVideo(const QJsonObject &event)
{
  if (state_ != State::Active || !video_ || !peerCameraEnabled_ ||
      event["call_id"].toString() != call_ || event["peer"].toInt() != peer_)
    return;
  const int sequence = event["seq"].toInt(-1);
  const auto bytes = event["image"].toString().toLatin1();
  if (sequence <= receivedVideo_ || bytes.isEmpty() || bytes.size() > 8000)
    return;
  const auto decoded =
      QByteArray::fromBase64Encoding(bytes, QByteArray::AbortOnBase64DecodingErrors);
  if (!decoded || decoded.decoded.isEmpty() || decoded.decoded.size() > 6000)
    return;
  receivedVideo_ = sequence;
  progress();
  emit videoFrame(decoded.decoded);
}
