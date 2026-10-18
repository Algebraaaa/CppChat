#pragma once
#include <cstdint>
#include <string>
#include <string_view>

// Canonical RIFF/WAVE PCM: 16 kHz, one channel, signed 16-bit little endian.
// Container validation only; capture/playback stays in Qt Multimedia.
namespace cppchat::media {
constexpr std::uint32_t WaveHeaderBytes = 44;
constexpr std::uint32_t PcmBytesPerSecond = 32000;
constexpr std::uint32_t MinPcmBytes = PcmBytesPerSecond;
constexpr std::uint32_t MaxPcmBytes = PcmBytesPerSecond * 60;
inline std::uint32_t ReadLittle(std::string_view bytes, std::size_t offset, int count)
{
  std::uint32_t value = 0;
  for (int i = 0; i < count; ++i)
    value |= std::uint32_t(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
  return value;
}
inline int WaveDurationMs(std::string_view header, std::uint64_t totalBytes)
{
  if (header.size() < WaveHeaderBytes || totalBytes < WaveHeaderBytes + MinPcmBytes ||
      totalBytes > WaveHeaderBytes + MaxPcmBytes || (totalBytes - WaveHeaderBytes) % 2 != 0)
    return 0;
  if (header.substr(0, 4) != "RIFF" || ReadLittle(header, 4, 4) != totalBytes - 8 ||
      header.substr(8, 8) != "WAVEfmt " || ReadLittle(header, 16, 4) != 16 ||
      ReadLittle(header, 20, 2) != 1 || ReadLittle(header, 22, 2) != 1 ||
      ReadLittle(header, 24, 4) != 16000 || ReadLittle(header, 28, 4) != PcmBytesPerSecond ||
      ReadLittle(header, 32, 2) != 2 || ReadLittle(header, 34, 2) != 16 ||
      header.substr(36, 4) != "data" || ReadLittle(header, 40, 4) != totalBytes - WaveHeaderBytes)
    return 0;
  return int((totalBytes - WaveHeaderBytes) * 1000 / PcmBytesPerSecond);
}
inline std::string WaveHeader(std::uint32_t pcmBytes)
{
  if (pcmBytes < MinPcmBytes || pcmBytes > MaxPcmBytes || pcmBytes % 2 != 0)
    return {};
  std::string header(WaveHeaderBytes, '\0');
  const auto write = [&header](std::size_t offset, std::uint32_t value, int count) {
    for (int i = 0; i < count; ++i)
      header[offset + i] = char((value >> (8 * i)) & 255);
  };
  header.replace(0, 4, "RIFF");
  write(4, pcmBytes + 36, 4);
  header.replace(8, 8, "WAVEfmt ");
  write(16, 16, 4);
  write(20, 1, 2);
  write(22, 1, 2);
  write(24, 16000, 4);
  write(28, PcmBytesPerSecond, 4);
  write(32, 2, 2);
  write(34, 16, 2);
  header.replace(36, 4, "data");
  write(40, pcmBytes, 4);
  return header;
}
} // namespace cppchat::media
