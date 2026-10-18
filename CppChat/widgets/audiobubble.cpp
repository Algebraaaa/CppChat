#include "audiobubble.h"
#include "common/chatstore.h"
#include "network/resourcemgr.h"
#include "services/voicemessageaudio.h"
#include <QHideEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>

AudioBubble::AudioBubble(ChatRole role, QWidget *parent) : BubbleFrame(role, parent)
{
  setObjectName("audioBubble");
  _key = "audio:" + QUuid::createUuid().toString();
  auto *body = new QWidget(this);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(10, 8, 10, 8);
  _play = new QPushButton(tr("播放语音"), body);
  _play->setObjectName("audioMessagePlay");
  _status = new QLabel(tr("点击加载并播放；最长 60 秒"), body);
  _status->setTextFormat(Qt::PlainText);
  _status->setWordWrap(true);
  layout->addWidget(_play);
  layout->addWidget(_status);
  setWidget(body);
  connect(_play, &QPushButton::clicked, this, &AudioBubble::Toggle);
  auto *audio = VoiceMessageAudio::instance();
  connect(audio, &VoiceMessageAudio::changed, this, [this, audio] {
    _play->setText(audio->PlayingKey() == _key ? tr("停止播放") : tr("播放语音"));
    if (audio->CallBusy()) {
      ++_revision;
      _loading = false;
    }
  });
  connect(audio, &VoiceMessageAudio::failed, this, [this](const QString &error) {
    if (isVisible())
      _status->setText(error);
  });
  connect(ChatStore::instance(), &ChatStore::accountChanged, this, [this] {
    ++_revision;
    _wave.clear();
    _loading = false;
    _play->setEnabled(false);
  });
  connect(ResourceMgr::instance(), &ResourceMgr::imageReady, this,
          [this](const QString &file, const QByteArray &bytes, const QString &error) {
            if (_loading && _file == file && _generation == ChatStore::instance()->Generation())
              Ready(bytes, error);
          });
}
AudioBubble::~AudioBubble()
{
  if (VoiceMessageAudio::instance()->PlayingKey() == _key)
    VoiceMessageAudio::instance()->Cancel();
}
void AudioBubble::hideEvent(QHideEvent *event)
{
  ++_revision;
  _loading = false;
  if (VoiceMessageAudio::instance()->PlayingKey() == _key)
    VoiceMessageAudio::instance()->Cancel();
  BubbleFrame::hideEvent(event);
}
void AudioBubble::Load(const QString &fileId)
{
  ++_revision;
  _file = fileId;
  _wave.clear();
  _loading = false;
}
void AudioBubble::SetSnapshot(const QByteArray &wave)
{
  ++_revision;
  _wave = wave;
  _status->setText(tr("语音 %1 秒").arg(VoiceMessageAudio::DurationMs(wave) / 1000.0, 0, 'f', 1));
}
void AudioBubble::Toggle()
{
  auto *audio = VoiceMessageAudio::instance();
  if (audio->CallBusy() || audio->Recording()) {
    _status->setText(tr("请结束通话或录音后播放"));
    return;
  }
  if (!_wave.isEmpty()) {
    audio->Play(_wave, _key);
    return;
  }
  if (_loading || _file.isEmpty())
    return;
  _loading = true;
  _generation = ChatStore::instance()->Generation();
  _status->setText(tr("正在读取语音…"));
  const auto revision = _revision;
  const QPointer<AudioBubble> guard(this);
  ChatStore::instance()->LoadAttachment(_file, [guard, revision](const ChatStoreResult &result) {
    if (!guard || guard->_revision != revision || !guard->_loading ||
        guard->_generation != ChatStore::instance()->Generation() || !guard->isVisible())
      return;
    if (VoiceMessageAudio::instance()->CallBusy() || VoiceMessageAudio::instance()->Recording()) {
      guard->_loading = false;
      return;
    }
    if (result.error.isEmpty() && !result.image.isEmpty())
      guard->Ready(result.image, {});
    else {
      const auto cached = ResourceMgr::instance()->cachedMedia(guard->_file);
      if (!cached.isEmpty())
        guard->Ready(cached, {});
      else
        ResourceMgr::instance()->fetchMedia(guard->_file);
    }
  });
}
void AudioBubble::Ready(const QByteArray &wave, const QString &error)
{
  _loading = false;
  if (!isVisible() || _generation != ChatStore::instance()->Generation())
    return;
  if (!error.isEmpty() || VoiceMessageAudio::DurationMs(wave) == 0) {
    _status->setText(tr("语音读取失败或格式无效，请点击重试"));
    return;
  }
  _wave = wave;
  _status->setText(tr("语音 %1 秒").arg(VoiceMessageAudio::DurationMs(wave) / 1000.0, 0, 'f', 1));
  // Device ownership is rechecked after asynchronous loads; this never records.
  VoiceMessageAudio::instance()->Play(_wave, _key);
}
