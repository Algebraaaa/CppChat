#pragma once

#include "BusinessExecutor.h"
#include <chrono>
#include <mutex>

// 仅决定日志汇总时机，不改变计数采集、心跳或节点健康租约
class MetricsLogPolicy final {
public:
  using Clock = std::chrono::steady_clock;
  enum class Decision { Quiet, Summary, Warning };

  explicit MetricsLogPolicy(Clock::time_point now = Clock::now()) : last_summary_(now) {}

  void SetInterval(std::chrono::seconds interval)
  {
    std::lock_guard<std::mutex> guard(mutex_);
    interval_ = interval;
  }

  Decision Check(const BusinessMetrics &current, Clock::time_point now, bool force = false)
  {
    std::lock_guard<std::mutex> guard(mutex_);
    const bool errors =
        current.failed != reported_.failed || current.rejected != reported_.rejected;
    const bool activity = errors || current.completed != reported_.completed || current.queued ||
                          current.active || reported_.queued || reported_.active;
    if (!activity)
      return Decision::Quiet;
    if (errors) {
      if (!force && warned_ && now - last_warning_ < std::chrono::seconds(60))
        return Decision::Quiet;
      last_warning_ = now;
      warned_ = true;
    } else if (!force && now - last_summary_ < interval_) {
      return Decision::Quiet;
    }
    reported_ = current;
    last_summary_ = now;
    return errors ? Decision::Warning : Decision::Summary;
  }

private:
  std::mutex mutex_;
  BusinessMetrics reported_;
  Clock::time_point last_summary_;
  Clock::time_point last_warning_{};
  std::chrono::seconds interval_{300};
  bool warned_ = false;
};
