#include "voicemessagedialog.h"
#include "common/chatstore.h"
#include "network/tcpmgr.h"
#include "services/voicemessageaudio.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>

VoiceMessageDialog::VoiceMessageDialog(QWidget *parent) : QDialog(parent)
{
  setObjectName("voiceMessageDialog");
  setProperty("groupDialog", true);
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowModality(Qt::WindowModal);
  setWindowTitle(tr("录制语音消息"));
  resize(440, 220);
  _previewKey = "record-preview:" + QUuid::createUuid().toString();
  auto *layout = new QVBoxLayout(this);
  _status = new QLabel(tr("点击开始录音。最长 60 秒；录好后可试听或取消。"), this);
  _status->setWordWrap(true);
  _status->setTextFormat(Qt::PlainText);
  _status->setObjectName("voiceMessageStatus");
  layout->addWidget(_status);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
  _record = buttons->addButton(tr("开始录音"), QDialogButtonBox::ActionRole);
  _stop = buttons->addButton(tr("停止录音"), QDialogButtonBox::ActionRole);
  _preview = buttons->addButton(tr("试听 / 停止"), QDialogButtonBox::ActionRole);
  _send = buttons->addButton(tr("发送"), QDialogButtonBox::ActionRole);
  _record->setObjectName("voiceMessageRecord");
  _stop->setObjectName("voiceMessageStop");
  _preview->setObjectName("voiceMessagePreview");
  _send->setObjectName("voiceMessageSend");
  layout->addWidget(buttons);
  auto *audio = VoiceMessageAudio::instance();
  connect(_record, &QPushButton::clicked, this, [this, audio] {
    if (audio->StartRecording()) {
      _recording = true;
      _wave.clear();
      _status->setText(tr("正在录音… 0 秒"));
      Refresh();
    }
  });
  connect(_stop, &QPushButton::clicked, audio, &VoiceMessageAudio::StopRecording);
  connect(audio, &VoiceMessageAudio::recordingProgress, this, [this](int ms) {
    if (_recording)
      _status->setText(tr("正在录音… %1 / 60 秒").arg(ms / 1000.0, 0, 'f', 1));
  });
  connect(audio, &VoiceMessageAudio::recorded, this, [this](const QByteArray &wave) {
    if (!_recording)
      return;
    _recording = false;
    _wave = wave;
    _status->setText(tr("录音完成：%1 秒，可试听后发送。")
                         .arg(VoiceMessageAudio::DurationMs(wave) / 1000.0, 0, 'f', 1));
    Refresh();
  });
  connect(audio, &VoiceMessageAudio::failed, this, [this](const QString &error) {
    _recording = false;
    _status->setText(error);
    Refresh();
  });
  connect(audio, &VoiceMessageAudio::changed, this, [this, audio] {
    if (audio->CallBusy())
      reject();
    else
      Refresh();
  });
  connect(_preview, &QPushButton::clicked, this,
          [this, audio] { audio->Play(_wave, _previewKey); });
  connect(_send, &QPushButton::clicked, this, [this, audio] {
    if (_recording || _sending || audio->CallBusy() || VoiceMessageAudio::DurationMs(_wave) == 0)
      return;
    SetSending(true);
    emit sendRequested(_wave);
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(this, &QDialog::finished, this, &VoiceMessageDialog::CancelDevices);
  connect(ChatStore::instance(), &ChatStore::accountChanged, this, &QDialog::reject);
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_connection_closed, this, &QDialog::reject);
  Refresh();
}
VoiceMessageDialog::~VoiceMessageDialog() { CancelDevices(); }
void VoiceMessageDialog::CancelDevices()
{
  auto *audio = VoiceMessageAudio::instance();
  if ((_recording && audio->Recording()) || audio->PlayingKey() == _previewKey)
    audio->Cancel();
  _recording = false;
  _wave.clear();
}
void VoiceMessageDialog::Refresh()
{
  const auto *audio = VoiceMessageAudio::instance();
  const bool ready = !_sending && !_recording && !audio->CallBusy() && !_wave.isEmpty();
  _record->setEnabled(!_sending && !_recording && !audio->CallBusy());
  _stop->setEnabled(_recording);
  _preview->setEnabled(ready);
  _send->setEnabled(ready);
}
void VoiceMessageDialog::SetSending(bool sending)
{
  _sending = sending;
  _status->setText(sending ? tr("正在保存待发送语音…") : tr("发送未完成，录音仍在，可重试或取消"));
  Refresh();
}
