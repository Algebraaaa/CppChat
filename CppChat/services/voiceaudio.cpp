#include "voiceaudio.h"
#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <opus.h>

namespace {
constexpr int Samples = 960;
QAudioFormat selectFormat(const QAudioDevice &device)
{
  for (const auto sample : {QAudioFormat::Int16, QAudioFormat::Float})
    for (int channels : {1, 2}) {
      QAudioFormat format;
      format.setSampleRate(48000);
      format.setChannelCount(channels);
      format.setSampleFormat(sample);
      if (device.isFormatSupported(format))
        return format;
    }
  return {};
}
float readSample(const char *p, QAudioFormat::SampleFormat format)
{
  if (format == QAudioFormat::Float) {
    float sample;
    std::memcpy(&sample, p, sizeof(sample));
    return std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0.0f;
  }
  qint16 sample;
  std::memcpy(&sample, p, sizeof(sample));
  return sample / 32768.0f;
}
QByteArray monoPcm(const QByteArray &input, const QAudioFormat &format)
{
  QByteArray result(Samples * 2, Qt::Uninitialized);
  for (int i = 0; i < Samples; ++i) {
    float sum = 0;
    for (int ch = 0; ch < format.channelCount(); ++ch)
      sum +=
          readSample(input.constData() + i * format.bytesPerFrame() + ch * format.bytesPerSample(),
                     format.sampleFormat());
    const qint16 sample = qint16(std::clamp(sum / format.channelCount(), -1.0f, 0.999969f) * 32768);
    std::memcpy(result.data() + 2 * i, &sample, 2);
  }
  return result;
}
QByteArray outputPcm(const QByteArray &mono, const QAudioFormat &format)
{
  QByteArray result(Samples * format.bytesPerFrame(), Qt::Uninitialized);
  for (int i = 0; i < Samples; ++i) {
    qint16 sample;
    std::memcpy(&sample, mono.constData() + i * 2, 2);
    for (int ch = 0; ch < format.channelCount(); ++ch) {
      auto *p = result.data() + i * format.bytesPerFrame() + ch * format.bytesPerSample();
      if (format.sampleFormat() == QAudioFormat::Float) {
        const float value = sample / 32768.0f;
        std::memcpy(p, &value, 4);
      } else
        std::memcpy(p, &sample, 2);
    }
  }
  return result;
}
} // namespace
VoiceAudio::VoiceAudio(QObject *parent) : QObject(parent)
{
  playout_.setInterval(20);
  playout_.setTimerType(Qt::PreciseTimer);
  connect(&playout_, &QTimer::timeout, this, &VoiceAudio::play);
}
VoiceAudio::~VoiceAudio() { stop(); }
QByteArray VoiceAudio::encodeFrame(OpusEncoder *encoder, const QByteArray &pcm)
{
  if (!encoder || pcm.size() != Samples * 2)
    return {};
  opus_int16 samples[Samples];
  std::memcpy(samples, pcm.constData(), sizeof(samples));
  QByteArray packet(512, Qt::Uninitialized);
  const int size =
      opus_encode(encoder, samples, Samples, reinterpret_cast<unsigned char *>(packet.data()),
                  int(packet.size()));
  if (size < 0)
    return {};
  packet.resize(size);
  return packet;
}
QByteArray VoiceAudio::decodeFrame(OpusDecoder *decoder, const QByteArray &packet)
{
  if (!decoder || packet.size() > 512)
    return {};
  const auto *data = reinterpret_cast<const unsigned char *>(packet.constData());
  if (!packet.isEmpty() && opus_packet_get_nb_samples(data, int(packet.size()), 48000) != Samples)
    return {};
  opus_int16 samples[Samples];
  const int size = opus_decode(decoder, packet.isEmpty() ? nullptr : data, int(packet.size()),
                               samples, Samples, 0);
  return size == Samples ? QByteArray(reinterpret_cast<const char *>(samples), sizeof(samples))
                         : QByteArray();
}
bool VoiceAudio::start(QString &error)
{
  stop();
  const auto input = QMediaDevices::defaultAudioInput(),
             output = QMediaDevices::defaultAudioOutput();
  inputFormat_ = selectFormat(input);
  outputFormat_ = selectFormat(output);
  if (input.isNull() || output.isNull() || !inputFormat_.isValid() || !outputFormat_.isValid()) {
    error = tr("找不到可用的 48kHz 麦克风或扬声器，请检查设备及系统麦克风权限");
    return false;
  }
  int code = 0;
  encoder_ = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &code);
  decoder_ = opus_decoder_create(48000, 1, &code);
  if (!encoder_ || !decoder_) {
    error = tr("无法初始化语音编解码器");
    stop();
    return false;
  }
  opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(32000));
  opus_encoder_ctl(encoder_, OPUS_SET_COMPLEXITY(5));
  input_ = new QAudioSource(input, inputFormat_, this);
  output_ = new QAudioSink(output, outputFormat_, this);
  input_->setBufferSize(Samples * inputFormat_.bytesPerFrame() * 3);
  output_->setBufferSize(Samples * outputFormat_.bytesPerFrame() * 3);
  const auto generation = generation_;
  auto watch = [this, generation](QAudio::State) {
    if ((input_ && input_->error() != QAudio::NoError) ||
        (output_ && output_->error() != QAudio::NoError &&
         output_->error() != QAudio::UnderrunError))
      QTimer::singleShot(0, this, [this, generation] {
        if (input_ && generation == generation_)
          emit failed(tr("音频设备不可用或已被移除"));
      });
  };
  connect(input_, &QAudioSource::stateChanged, this, watch);
  connect(output_, &QAudioSink::stateChanged, this, watch);
  playback_ = output_->start();
  capture_ = input_->start();
  if (!playback_ || !capture_) {
    error = tr("无法打开音频设备，请检查麦克风权限或设备占用");
    stop();
    return false;
  }
  connect(capture_, &QIODevice::readyRead, this, &VoiceAudio::capture);
  playout_.start();
  return true;
}
void VoiceAudio::stop()
{
  ++generation_;
  playout_.stop();
  if (input_) {
    disconnect(input_, nullptr, this, nullptr);
    input_->stop();
    input_->deleteLater();
  }
  if (output_) {
    disconnect(output_, nullptr, this, nullptr);
    output_->stop();
    output_->deleteLater();
  }
  input_ = nullptr;
  output_ = nullptr;
  capture_ = playback_ = nullptr;
  if (encoder_)
    opus_encoder_destroy(encoder_);
  if (decoder_)
    opus_decoder_destroy(decoder_);
  encoder_ = nullptr;
  decoder_ = nullptr;
  captureBuffer_.clear();
  playbackBuffer_.clear();
  packets_.clear();
  lastSequence_ = 0;
  muted_ = primed_ = false;
}
void VoiceAudio::capture()
{
  const int bytes = Samples * inputFormat_.bytesPerFrame();
  captureBuffer_.append(capture_->readAll());
  if (captureBuffer_.size() > bytes * 3)
    captureBuffer_.remove(0, ((captureBuffer_.size() / bytes) - 2) * bytes);
  while (captureBuffer_.size() >= bytes) {
    const auto pcm = muted_ ? QByteArray(Samples * 2, '\0') : monoPcm(captureBuffer_, inputFormat_);
    captureBuffer_.remove(0, bytes);
    const auto packet = encodeFrame(encoder_, pcm);
    if (!packet.isEmpty())
      emit encoded(packet);
    if (!capture_)
      return; // Synchronous transport failure may tear down the call.
  }
}
void VoiceAudio::receive(const QByteArray &opus, int sequence)
{
  if (!decoder_ || sequence <= lastSequence_)
    return;
  // Missing frames use Opus PLC; cap concealment and latency after a long gap.
  for (int i = lastSequence_ + 1; lastSequence_ > 0 && i < sequence && i < lastSequence_ + 3; ++i)
    packets_.enqueue({});
  lastSequence_ = sequence;
  packets_.enqueue(opus);
  while (packets_.size() > 6)
    packets_.dequeue();
}
void VoiceAudio::play()
{
  if (!playback_ || !output_ || !decoder_)
    return;
  if (!primed_ && packets_.size() < 3)
    return;
  primed_ = true;
  const int bytes = Samples * outputFormat_.bytesPerFrame();
  if (playbackBuffer_.isEmpty() && output_->bytesFree() >= bytes) {
    const auto pcm = decodeFrame(decoder_, packets_.isEmpty() ? QByteArray() : packets_.dequeue());
    if (!pcm.isEmpty())
      playbackBuffer_ = outputPcm(pcm, outputFormat_);
  }
  if (!playbackBuffer_.isEmpty()) {
    const auto written = playback_->write(playbackBuffer_);
    if (written > 0)
      playbackBuffer_.remove(0, written);
  }
}
