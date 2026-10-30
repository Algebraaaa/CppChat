#include "InstanceLease.h"

#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace ServerLogging {
InstanceLease::InstanceLease(const std::filesystem::path &directory)
{
  // 复用空闲槽位，重启不按 PID 累积新目录；不会清理其他进程的文件
  for (unsigned slot = 0; slot < 16; ++slot) {
    const auto candidate = directory / ("slot-" + std::to_string(slot));
    std::filesystem::create_directories(candidate);
    const auto lock = candidate / ".owner.lock";
#ifdef _WIN32
    const auto handle = CreateFileW(lock.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
      continue;
    handle_ = handle;
#else
    const int descriptor = open(lock.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (descriptor < 0)
      continue;
    if (flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
      close(descriptor);
      continue;
    }
    descriptor_ = descriptor;
#endif
    directory_ = candidate;
    return;
  }
  throw std::runtime_error("No writable log slot available (limit 16); using console only");
}

InstanceLease::~InstanceLease()
{
#ifdef _WIN32
  if (handle_)
    CloseHandle(handle_);
#else
  if (descriptor_ >= 0)
    close(descriptor_);
#endif
}
} // namespace ServerLogging
