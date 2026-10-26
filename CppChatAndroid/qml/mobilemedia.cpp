#include "mobilemedia.h"
#include "common/chatstore.h"
#include "network/resourcemgr.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/videomedia.h"
#include "services/voiceaudio.h"
#include "services/voicecallservice.h"
#include "services/voicemessageaudio.h"
#include <QGuiApplication>
#if QT_CONFIG(permissions)
#include <QPermissions>
#endif
#include <QVideoFrame>

MobileMedia::MobileMedia(QObject *parent) : QObject(parent), m_call(new VoiceCallService(this)),
    m_audio(new VoiceAudio(this)), m_video(new VideoMedia(this))
{
  const auto tcp = TcpMgr::GetInstance();
  connect(m_call, &VoiceCallService::send, tcp.get(), &TcpMgr::SendVoice);
  connect(m_call, &VoiceCallService::sendVideoFrame, tcp.get(), &TcpMgr::SendVideo);
  connect(tcp.get(), &TcpMgr::sig_voice_event, m_call, &VoiceCallService::receive);
  connect(tcp.get(), &TcpMgr::sig_voice_audio, m_call, &VoiceCallService::receiveAudio);
  connect(tcp.get(), &TcpMgr::sig_video_image, m_call, &VoiceCallService::receiveVideo);
  connect(m_call, &VoiceCallService::audio, m_audio, &VoiceAudio::receive);
  connect(m_audio, &VoiceAudio::encoded, m_call, &VoiceCallService::sendAudio);
  connect(m_video, &VideoMedia::encoded, m_call, &VoiceCallService::sendVideo);
  connect(m_audio, &VoiceAudio::failed, m_call, &VoiceCallService::finish);
  connect(m_video, &VideoMedia::failed, m_call, &VoiceCallService::finish);
  connect(m_call, &VoiceCallService::changed, this, &MobileMedia::refreshCall);
  connect(m_call, &VoiceCallService::networkChanged, this, &MobileMedia::changed);
  connect(m_call, &VoiceCallService::ended, this, &MobileMedia::status);
  connect(m_video, &VideoMedia::preview, this, [this](const QImage &image) {
    if (m_local)
      m_local->setVideoFrame(image.isNull() ? QVideoFrame() : QVideoFrame(image));
  });
  connect(m_call, &VoiceCallService::videoFrame, this, [this](const QByteArray &bytes) {
    const auto image = VideoMedia::decodeFrame(bytes);
    if (m_remote && !image.isNull())
      m_remote->setVideoFrame(QVideoFrame(image));
  });
  connect(m_call, &VoiceCallService::peerCameraChanged, this, [this](bool enabled) {
    m_peerCamera = enabled;
    if (!enabled && m_remote)
      m_remote->setVideoFrame({});
    emit changed();
  });
  auto *voice = VoiceMessageAudio::instance();
  connect(voice, &VoiceMessageAudio::changed, this, &MobileMedia::changed);
  connect(voice, &VoiceMessageAudio::failed, this, &MobileMedia::failed);
  connect(voice, &VoiceMessageAudio::recordingProgress, this, [this](int ms) {
    m_recordingMs = ms;
    emit changed();
  });
  connect(voice, &VoiceMessageAudio::recorded, this, [this](const QByteArray &wave) {
    if (m_recordingThread > 0) {
      m_draft = wave;
      emit changed();
    }
  });
  connect(ResourceMgr::instance(), &ResourceMgr::imageReady, this, &MobileMedia::voiceReady);
  connect(ChatStore::instance(), &ChatStore::accountChanged, this, &MobileMedia::reset);
  connect(tcp.get(), &TcpMgr::sig_connection_closed, this, &MobileMedia::reset);
  connect(tcp.get(), &TcpMgr::sig_notify_offline, this, &MobileMedia::reset);
  connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
    if (state == Qt::ApplicationSuspended || state == Qt::ApplicationHidden) {
      // Foreground-only capture: permission dialogs may make the app Inactive.
      endCall();
      discardRecording();
    }
  });
  connect(qGuiApp, &QCoreApplication::aboutToQuit, this, &MobileMedia::reset);
  m_tick.setInterval(1000);
  connect(&m_tick, &QTimer::timeout, this, &MobileMedia::changed);
}

MobileMedia::~MobileMedia() { reset(); }
bool MobileMedia::recording() const { return VoiceMessageAudio::instance()->Recording(); }
QString MobileMedia::playing() const { return VoiceMessageAudio::instance()->PlayingKey(); }
int MobileMedia::draftMs() const { return VoiceMessageAudio::DurationMs(m_draft); }
bool MobileMedia::busy() const { return m_call->state() != VoiceCallService::State::Idle; }
QVariantMap MobileMedia::call() const
{
  const auto user = UserMgr::GetInstance()->GetFriendById(m_call->peer());
  return {{"state", int(m_call->state())}, {"peer", m_call->peer()},
          {"name", user ? (user->_back.isEmpty() ? (user->_nick.isEmpty() ? user->_name : user->_nick) : user->_back) : tr("联系人")},
          {"video", m_call->isVideo()}, {"recovering", m_call->recovering()},
          {"muted", m_muted}, {"camera", m_camera},
          {"peerCamera", m_peerCamera}, {"front", m_front},
          {"seconds", m_started && m_duration.isValid() ? m_duration.elapsed() / 1000 : 0}};
}

void MobileMedia::requestPermissions(bool video, std::function<void()> action)
{
  if (m_permissionPending)
    return;
  m_permissionPending = true;
  emit changed();
  checkPermissions(video, ++m_permissionRevision, std::move(action));
}
void MobileMedia::checkPermissions(bool video, quint64 revision, std::function<void()> action)
{
  if (revision != m_permissionRevision)
    return;
#if QT_CONFIG(permissions)
  const QMicrophonePermission microphone;
  const QCameraPermission camera;
  const auto micStatus = qApp->checkPermission(microphone);
  const auto camStatus = video ? qApp->checkPermission(camera) : Qt::PermissionStatus::Granted;
  if (micStatus == Qt::PermissionStatus::Denied || camStatus == Qt::PermissionStatus::Denied) {
    m_permissionPending = false;
    emit changed();
    emit failed(tr("需要麦克风权限；视频还需要摄像头权限。请在系统应用设置中允许后重试。"));
    return;
  }
  const auto ready = [this, video, revision, action](const QPermission &) {
    checkPermissions(video, revision, action);
  };
  if (micStatus == Qt::PermissionStatus::Undetermined) {
    qApp->requestPermission(microphone, this, ready);
    return;
  }
  if (camStatus == Qt::PermissionStatus::Undetermined) {
    qApp->requestPermission(camera, this, ready);
    return;
  }
#else
  Q_UNUSED(video)
#endif
  m_permissionPending = false;
  emit changed();
  if (QGuiApplication::applicationState() == Qt::ApplicationActive)
    action();
  else {
    // Android may deliver the permission result before returning focus to Qt.
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
        [this, revision, action, connection](Qt::ApplicationState state) {
          if (state != Qt::ApplicationActive)
            return;
          disconnect(*connection);
          if (revision == m_permissionRevision)
            action();
        });
  }
}
void MobileMedia::startCall(int peer, bool video)
{
  if (busy() || recording() || !m_draft.isEmpty() || m_permissionPending)
    return;
  requestPermissions(video, [this, peer, video] {
    const auto tcp = TcpMgr::GetInstance();
    if (!busy() && tcp->IsConnected() && tcp->SupportsVoiceCall() &&
        (!video || tcp->SupportsVideoCall()) && UserMgr::GetInstance()->CheckFriendById(peer))
      m_call->start(peer, video);
    else
      emit failed(tr("连接、好友关系或通话能力已变化，请重试"));
  });
}
void MobileMedia::acceptCall()
{
  if (m_call->state() != VoiceCallService::State::Incoming)
    return;
  const auto id = m_call->callId();
  requestPermissions(m_call->isVideo(), [this, id] {
    if (m_call->callId() == id && m_call->state() == VoiceCallService::State::Incoming)
      m_call->accept();
  });
}
void MobileMedia::endCall()
{
  ++m_permissionRevision;
  m_permissionPending = false;
  m_call->finish();
  emit changed();
}
void MobileMedia::refreshCall()
{
  const auto state = m_call->state();
  VoiceMessageAudio::instance()->SetCallBusy(state != VoiceCallService::State::Idle);
  m_loadingVoice.clear();
  if (state == VoiceCallService::State::Idle) {
    ++m_permissionRevision;
    m_permissionPending = false;
    m_audio->stop();
    m_video->stop();
    if (m_remote)
      m_remote->setVideoFrame({});
    m_started = m_muted = false;
    m_camera = m_peerCamera = m_front = true;
    m_tick.stop();
  } else if (state == VoiceCallService::State::Active && !m_started) {
    QString error;
    // The user granted permission before invite/accept. No silent background capture.
    if (QGuiApplication::applicationState() != Qt::ApplicationActive ||
        !m_audio->start(error) || (m_call->isVideo() && !m_video->start(error, m_front))) {
      m_call->finish(error.isEmpty() ? tr("应用已离开前台") : error);
      return;
    }
    if (m_call->state() != VoiceCallService::State::Active) {
      m_audio->stop();
      m_video->stop();
      return;
    }
    m_started = true;
    m_duration.start();
    m_tick.start();
  }
  emit changed();
}
void MobileMedia::setMuted(bool muted)
{
  if (!m_started)
    return;
  m_muted = muted;
  m_audio->setMuted(muted);
  emit changed();
}
void MobileMedia::setCameraEnabled(bool enabled)
{
  if (!m_started || !m_call->isVideo())
    return;
  QString error;
  if (enabled && !m_video->start(error, m_front)) {
    emit failed(error);
    return;
  }
  if (!enabled)
    m_video->stop();
  m_camera = enabled;
  m_call->setCameraEnabled(enabled);
  emit changed();
}
void MobileMedia::switchCamera()
{
  if (!m_started || !m_call->isVideo() || !m_camera)
    return;
  m_front = !m_front;
  QString error;
  if (!m_video->start(error, m_front))
    m_call->finish(error);
  emit changed();
}
void MobileMedia::setVideoSinks(QVideoSink *local, QVideoSink *remote)
{
  m_local = local;
  m_remote = remote;
}
void MobileMedia::startRecording(int thread)
{
  if (thread <= 0 || busy() || recording() || !m_draft.isEmpty() || m_permissionPending)
    return;
  m_recordingThread = thread;
  requestPermissions(false, [this, thread] {
    if (!busy() && m_recordingThread == thread && ChatStore::instance()->IsReady() &&
        UserMgr::GetInstance()->GetChatThreadByThreadId(thread)) {
      m_recordingMs = 0;
      m_loadingVoice.clear();
      VoiceMessageAudio::instance()->StartRecording();
      emit changed();
    }
  });
}
void MobileMedia::finishRecording() { VoiceMessageAudio::instance()->StopRecording(); }
void MobileMedia::clearDraft()
{
  m_draft.clear();
  m_recordingThread = m_recordingMs = 0;
  emit changed();
}
void MobileMedia::discardRecording()
{
  ++m_permissionRevision;
  m_permissionPending = false;
  m_loadingVoice.clear();
  VoiceMessageAudio::instance()->Cancel();
  clearDraft();
}
void MobileMedia::leaveChat(int thread)
{
  if (m_recordingThread == thread)
    discardRecording();
  m_loadingVoice.clear();
  VoiceMessageAudio::instance()->Cancel();
}
void MobileMedia::playVoice(const QString &fileId)
{
  if (busy() || recording() || m_permissionPending || fileId.isEmpty())
    return;
  if (playing() == fileId) {
    m_loadingVoice.clear();
    VoiceMessageAudio::instance()->Cancel();
    return;
  }
  m_loadingVoice = fileId;
  m_voiceGeneration = ChatStore::instance()->Generation();
  ResourceMgr::instance()->fetchMedia(fileId);
}
void MobileMedia::voiceReady(const QString &id, const QByteArray &bytes, const QString &error)
{
  if (id != m_loadingVoice || m_voiceGeneration != ChatStore::instance()->Generation())
    return;
  m_loadingVoice.clear();
  if (!error.isEmpty())
    emit failed(error);
  else if (!busy() && !recording())
    VoiceMessageAudio::instance()->Play(bytes, id);
}
void MobileMedia::reset()
{
  m_call->disconnected();
  m_audio->stop();
  m_video->stop();
  VoiceMessageAudio::instance()->SetCallBusy(false);
  discardRecording();
}
