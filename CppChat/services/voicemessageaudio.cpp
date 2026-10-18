#include "voicemessageaudio.h"
#include "common/chatstore.h"
#include "media/pcmwave.h"
#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QCoreApplication>
#include <QMediaDevices>

namespace {
QAudioFormat format()
{
  QAudioFormat value;
  value.setSampleRate(16000);
  value.setChannelCount(1);
  value.setSampleFormat(QAudioFormat::Int16);
  return value;
}
} // namespace
VoiceMessageAudio *VoiceMessageAudio::instance()
{
  static auto *audio = new VoiceMessageAudio(QCoreApplication::instance());
  return audio;
}
VoiceMessageAudio::VoiceMessageAudio(QObject *parent) : QObject(parent)
{
  _limit.setSingleShot(true);
  _progress.setInterval(100);
  connect(&_limit, &QTimer::timeout, this, [this] {
    if (Recording())
      StopRecording();
    else
      Cancel();
  });
  connect(&_progress, &QTimer::timeout, this, [this] {
    emit recordingProgress(int(_pcm.size() * 1000 / cppchat::media::PcmBytesPerSecond));
  });
  connect(ChatStore::instance(), &ChatStore::accountChanged, this, &VoiceMessageAudio::Cancel);
  auto *devices = new QMediaDevices(this);
  const auto changed = [this] {
    if (_input || _output) {
      Cancel();
      emit failed(tr("音频设备已改变，请检查设备后重试"));
    }
  };
  connect(devices, &QMediaDevices::audioInputsChanged, this, changed);
  connect(devices, &QMediaDevices::audioOutputsChanged, this, changed);
  connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
          &VoiceMessageAudio::Cancel);
}
VoiceMessageAudio::~VoiceMessageAudio() { StopDevices(); }
int VoiceMessageAudio::DurationMs(const QByteArray &wave)
{
  return cppchat::media::WaveDurationMs(std::string_view(wave.constData(), wave.size()),
                                        wave.size());
}
QByteArray VoiceMessageAudio::FromPcm(const QByteArray &pcm)
{
  if (pcm.size() < cppchat::media::MinPcmBytes || pcm.size() > cppchat::media::MaxPcmBytes)
    return {};
  const auto header = cppchat::media::WaveHeader(std::uint32_t(pcm.size()));
  return header.empty() ? QByteArray() : QByteArray(header.data(), header.size()) + pcm;
}
void VoiceMessageAudio::StopDevices()
{
  ++_revision;
  _limit.stop();
  _progress.stop();
  if (_input) {
    auto *input = _input;
    _input = nullptr;
    disconnect(input, nullptr, this, nullptr);
    if (_capture)
      disconnect(_capture, nullptr, this, nullptr);
    _capture = nullptr;
    input->stop();
    delete input;
  }
  if (_output) {
    auto *output = _output;
    _output = nullptr;
    disconnect(output, nullptr, this, nullptr);
    output->stop();
    delete output;
  }
  _playback.close();
  _playback.setData({});
  _playing.clear();
}
void VoiceMessageAudio::Cancel()
{
  StopDevices();
  _pcm.clear();
  emit changed();
}
void VoiceMessageAudio::SetCallBusy(bool busy)
{
  if (_callBusy == busy)
    return;
  _callBusy = busy;
  if (busy)
    Cancel();
  else
    emit changed();
}
bool VoiceMessageAudio::StartRecording()
{
  if (_callBusy || Recording()) {
    emit failed(tr("通话或录音进行中，暂时不能开始录制"));
    return false;
  }
  Cancel();
  const auto device = QMediaDevices::defaultAudioInput();
  if (device.isNull() || !device.isFormatSupported(format())) {
    emit failed(tr("麦克风不可用或不支持 16kHz 单声道 PCM，请检查设备及系统权限"));
    return false;
  }
  _input = new QAudioSource(device, format(), this);
  _input->setBufferSize(6400);
  const auto revision = _revision;
  connect(_input, &QAudioSource::stateChanged, this, [this, revision](QAudio::State) {
    if (_input && _input->error() != QAudio::NoError)
      QTimer::singleShot(0, this, [this, revision] {
        if (_revision != revision)
          return;
        Cancel();
        emit failed(tr("录音设备异常，录音已取消"));
      });
  });
  _capture = _input->start();
  if (!_capture || _input->error() != QAudio::NoError) {
    Cancel();
    emit failed(tr("无法启动麦克风，请检查权限和设备占用"));
    return false;
  }
  connect(_capture, &QIODevice::readyRead, this, &VoiceMessageAudio::Capture);
  _limit.start(60000);
  _progress.start();
  emit changed();
  return true;
}
void VoiceMessageAudio::Capture()
{
  if (!_capture)
    return;
  const auto remaining = qint64(cppchat::media::MaxPcmBytes) - _pcm.size();
  _pcm += _capture->read(remaining);
  if (_pcm.size() >= cppchat::media::MaxPcmBytes) {
    const auto revision = _revision;
    QTimer::singleShot(0, this, [this, revision] {
      if (_revision == revision && Recording())
        StopRecording();
    });
  }
}
void VoiceMessageAudio::StopRecording()
{
  if (!Recording())
    return;
  Capture();
  _pcm.truncate(_pcm.size() - _pcm.size() % 2);
  const auto wave = FromPcm(_pcm);
  Cancel();
  if (wave.isEmpty())
    emit failed(tr("录音不足 1 秒，已取消"));
  else
    emit recorded(wave);
}
bool VoiceMessageAudio::Play(const QByteArray &wave, const QString &key)
{
  if (_callBusy || Recording()) {
    emit failed(tr("请结束录音或通话后再播放语音"));
    return false;
  }
  if (key.isEmpty() || DurationMs(wave) == 0) {
    emit failed(tr("语音文件无效或时长超出限制"));
    return false;
  }
  const bool toggle = _playing == key;
  Cancel();
  if (toggle)
    return true;
  const auto device = QMediaDevices::defaultAudioOutput();
  if (device.isNull() || !device.isFormatSupported(format())) {
    emit failed(tr("扬声器不可用或不支持语音格式"));
    return false;
  }
  _playback.setData(wave.mid(cppchat::media::WaveHeaderBytes));
  _playback.open(QIODevice::ReadOnly);
  _playing = key;
  _output = new QAudioSink(device, format(), this);
  const auto revision = _revision;
  connect(_output, &QAudioSink::stateChanged, this, [this, revision](QAudio::State state) {
    if (state == QAudio::IdleState || (_output && _output->error() != QAudio::NoError)) {
      const bool failed = _output && _output->error() != QAudio::NoError;
      QTimer::singleShot(0, this, [this, revision, failed] {
        if (_revision != revision)
          return;
        Cancel();
        if (failed)
          emit this->failed(tr("播放设备异常，请检查后重试"));
      });
    }
  });
  _output->start(&_playback);
  _limit.start(DurationMs(wave) + 2000);
  emit changed();
  return true;
}
