#include "voicecalldialog.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/videomedia.h"
#include "services/voiceaudio.h"
#include "services/voicecallservice.h"
#include "services/voicemessageaudio.h"
#include "ui_voicecalldialog.h"
#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>

VoiceCallDialog::VoiceCallDialog(QWidget *parent) : QDialog(parent)
{
  Ui::VoiceCallDialog ui;
  ui.setupUi(this);
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  name_ = ui.nameLabel;
  state_ = ui.stateLabel;
  accept_ = ui.voice_accept;
  end_ = ui.voice_end;
  mute_ = ui.voice_mute;
  service_ = new VoiceCallService(this);
  audio_ = new VoiceAudio(this);
  video_ = new VideoMedia(this);
  local_ = ui.localVideo;
  remote_ = ui.remoteVideo;
  camera_ = ui.cameraButton;
  local_->setAccessibleName(tr("本地摄像头预览"));
  remote_->setAccessibleName(tr("对方视频画面"));
  ui.videoPanel->hide();
  camera_->hide();
  auto tcp = TcpMgr::GetInstance();
  connect(service_, &VoiceCallService::send, tcp.get(), &TcpMgr::SendVoice);
  connectVideo(tcp.get());
  connect(tcp.get(), &TcpMgr::sig_voice_event, service_, &VoiceCallService::receive);
  connect(tcp.get(), &TcpMgr::sig_voice_audio, service_, &VoiceCallService::receiveAudio);
  connect(tcp.get(), &TcpMgr::sig_connection_closed, service_, &VoiceCallService::disconnected);
  connect(tcp.get(), &TcpMgr::sig_notify_offline, service_, &VoiceCallService::disconnected);
  connect(tcp.get(), &TcpMgr::sig_friend_deleted, this, [this](int uid) {
    if (uid == service_->peer())
      service_->finish(tr("好友关系已解除"));
  });
  connect(service_, &VoiceCallService::audio, audio_, &VoiceAudio::receive);
  connect(audio_, &VoiceAudio::encoded, service_, &VoiceCallService::sendAudio);
  connect(audio_, &VoiceAudio::failed, service_, &VoiceCallService::finish);
  connect(service_, &VoiceCallService::changed, this, &VoiceCallDialog::refresh);
  connect(service_, &VoiceCallService::ended, this, &VoiceCallDialog::status);
  connect(service_, &VoiceCallService::networkChanged, this, [this](bool recovering) {
    if (service_->state() == VoiceCallService::State::Active)
      state_->setText(recovering ? tr("网络波动，正在恢复…") : tr("通话中"));
  });
  connect(accept_, &QPushButton::clicked, service_, &VoiceCallService::accept);
  connect(end_, &QPushButton::clicked, this, &VoiceCallDialog::finish);
  connect(mute_, &QPushButton::toggled, this, [this](bool muted) {
    audio_->setMuted(muted);
    mute_->setText(muted ? tr("恢复麦克风") : tr("静音麦克风"));
  });
  timer_.setInterval(1000);
  connect(&timer_, &QTimer::timeout, this, [this] {
    if (service_->recovering()) {
      state_->setText(tr("网络波动，正在恢复…"));
      return;
    }
    const auto seconds = duration_.elapsed() / 1000;
    state_->setText(tr("通话中 %1:%2")
                        .arg(seconds / 60, 2, 10, QLatin1Char('0'))
                        .arg(seconds % 60, 2, 10, QLatin1Char('0')));
  });
}
void VoiceCallDialog::connectVideo(TcpMgr *tcp)
{
  connect(service_, &VoiceCallService::sendVideoFrame, tcp, &TcpMgr::SendVideo);
  connect(tcp, &TcpMgr::sig_video_image, service_, &VoiceCallService::receiveVideo);
  connect(video_, &VideoMedia::encoded, service_, &VoiceCallService::sendVideo);
  connect(video_, &VideoMedia::failed, service_, &VoiceCallService::finish);
  connect(video_, &VideoMedia::preview, this, [this](const QImage &image) {
    if (image.isNull()) {
      local_->clear();
      local_->setText(tr("摄像头已关闭"));
    } else
      local_->setPixmap(QPixmap::fromImage(image).scaled(160, 120, Qt::KeepAspectRatio));
  });
  connect(service_, &VoiceCallService::videoFrame, this, [this](const QByteArray &jpeg) {
    const auto image = VideoMedia::decodeFrame(jpeg);
    if (!image.isNull())
      remote_->setPixmap(QPixmap::fromImage(image));
  });
  connect(service_, &VoiceCallService::peerCameraChanged, this, [this](bool enabled) {
    remote_->clear();
    remote_->setText(enabled ? tr("等待对方画面") : tr("对方已关闭摄像头"));
  });
  connect(camera_, &QPushButton::toggled, this, [this](bool disabled) {
    if (service_->state() != VoiceCallService::State::Active || !service_->isVideo())
      return;
    if (disabled)
      video_->stop();
    else {
      QString error;
      if (!video_->start(error)) {
        service_->finish(error);
        return;
      }
    }
    service_->setCameraEnabled(!disabled);
    camera_->setText(disabled ? tr("开启摄像头") : tr("关闭摄像头"));
  });
}
VoiceCallDialog::~VoiceCallDialog()
{
  service_->finish();
  audio_->stop();
  video_->stop();
}
void VoiceCallDialog::call(int peer, bool video)
{
  if (service_->state() != VoiceCallService::State::Idle) {
    show();
    raise();
    activateWindow();
    return;
  }
  const auto tcp = TcpMgr::GetInstance();
  if (!tcp->IsConnected() || !tcp->SupportsVoiceCall() || (video && !tcp->SupportsVideoCall())) {
    emit status(tr("服务器尚不支持本次通话，或聊天连接已断开"));
    return;
  }
  if (UserMgr::GetInstance()->CheckFriendById(peer))
    service_->start(peer, video);
}
void VoiceCallDialog::finish() { service_->finish(); }
void VoiceCallDialog::reject()
{
  service_->finish();
  QDialog::reject();
}
void VoiceCallDialog::closeEvent(QCloseEvent *event)
{
  service_->finish();
  event->accept();
}
void VoiceCallDialog::refresh()
{
  VoiceMessageAudio::instance()->SetCallBusy(service_->state() != VoiceCallService::State::Idle);
  const auto state = service_->state();
  if (state == VoiceCallService::State::Idle) {
    audio_->stop();
    video_->stop();
    remote_->clear();
    {
      QSignalBlocker blocker(camera_);
      camera_->setChecked(false);
    }
    camera_->setText(tr("关闭摄像头"));
    audioStarted_ = false;
    timer_.stop();
    mute_->setChecked(false);
    hide();
    return;
  }
  const bool video = service_->isVideo();
  setWindowTitle(video ? tr("视频通话") : tr("语音通话"));
  local_->parentWidget()->setVisible(video);
  camera_->setVisible(video && state == VoiceCallService::State::Active);
  if (state != VoiceCallService::State::Active && video) {
    remote_->clear();
    remote_->setText(tr("接通后显示对方画面"));
    local_->clear();
    local_->setText(tr("接通后开启摄像头"));
  }
  auto user = UserMgr::GetInstance()->GetFriendById(service_->peer());
  name_->setText(user ? (user->_back.isEmpty() ? user->_name : user->_back)
                      : tr("用户 %1").arg(service_->peer()));
  accept_->setVisible(state == VoiceCallService::State::Incoming);
  mute_->setVisible(state == VoiceCallService::State::Active);
  end_->setText(state == VoiceCallService::State::Incoming   ? tr("拒接")
                : state == VoiceCallService::State::Outgoing ? tr("取消呼叫")
                                                             : tr("挂断"));
  if (state == VoiceCallService::State::Active && !audioStarted_) {
    QString error;
    if (!audio_->start(error)) {
      service_->finish(error);
      return;
    }
    if (video && !video_->start(error)) {
      service_->finish(error);
      return;
    }
    if (service_->state() != VoiceCallService::State::Active) {
      audio_->stop();
      video_->stop();
      return;
    }
    audioStarted_ = true;
    duration_.start();
    timer_.start();
  }
  if (state == VoiceCallService::State::Incoming) {
    state_->setText(video ? tr("邀请你进行视频通话") : tr("邀请你进行语音通话"));
    QApplication::alert(this);
    QApplication::beep();
  } else if (state == VoiceCallService::State::Outgoing)
    state_->setText(tr("等待对方接听…"));
  else if (state == VoiceCallService::State::Connecting)
    state_->setText(tr("正在连接…"));
  else
    state_->setText(tr("通话中"));
  show();
  raise();
  activateWindow();
}
