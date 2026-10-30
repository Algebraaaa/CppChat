#pragma once

#include <filesystem>

namespace ServerLogging {
// 锁文件长期保留，只有持有独占租约的进程可以写入对应槽位
class InstanceLease final {
public:
  explicit InstanceLease(const std::filesystem::path &directory);
  ~InstanceLease();
  InstanceLease(const InstanceLease &) = delete;
  InstanceLease &operator=(const InstanceLease &) = delete;
  const std::filesystem::path &Directory() const { return directory_; }

private:
  std::filesystem::path directory_;
#ifdef _WIN32
  void *handle_ = nullptr;
#else
  int descriptor_ = -1;
#endif
};
} // namespace ServerLogging
