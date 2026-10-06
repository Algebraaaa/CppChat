#include "BusinessExecutor.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

BusinessExecutor::BusinessExecutor(std::size_t workers, std::size_t queue_limit)
    : queue_limit_(queue_limit) {
  if (workers == 0 || workers > 16 || queue_limit == 0)
    throw std::invalid_argument("Invalid business executor limits");
  for (std::size_t i = 0; i < workers; ++i)
    lanes_.push_back(std::make_unique<Lane>());
  try {
    for (std::size_t i = 0; i < workers; ++i)
      lanes_[i]->worker = std::thread([this, i] { RunLane(i); });
  } catch (...) {
    Stop();
    throw;
  }
}

BusinessExecutor::~BusinessExecutor() { Stop(); }

bool BusinessExecutor::Submit(const std::string &key,
                              std::function<void()> task) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!task || !accepting_ || metrics_.queued >= queue_limit_) {
    ++metrics_.rejected;
    return false;
  }
  auto &lane = *lanes_[std::hash<std::string>{}(key) % lanes_.size()];
  lane.tasks.push({std::move(task), std::chrono::steady_clock::now()});
  ++metrics_.queued;
  metrics_.high_water = std::max(metrics_.high_water, metrics_.queued);
  lane.ready.notify_one();
  return true;
}

BusinessMetrics BusinessExecutor::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return metrics_;
}

void BusinessExecutor::BeginStop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    accepting_ = false;
  }
  for (auto &lane : lanes_)
    lane->ready.notify_all();
}

bool BusinessExecutor::WaitIdleUntil(
    std::chrono::steady_clock::time_point deadline) {
  std::unique_lock<std::mutex> lock(mutex_);
  return idle_.wait_until(lock, deadline, [this] {
    return metrics_.queued == 0 && metrics_.active == 0;
  });
}

std::size_t BusinessExecutor::Stop(bool drain) {
  std::lock_guard<std::mutex> stop_lock(stop_mutex_);
  std::size_t discarded = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    accepting_ = false;
    if (!drain) {
      for (auto &lane : lanes_) {
        discarded += lane->tasks.size();
        while (!lane->tasks.empty())
          lane->tasks.pop();
      }
      metrics_.queued -= discarded;
    }
    idle_.notify_all();
  }
  for (auto &lane : lanes_)
    lane->ready.notify_all();
  for (auto &lane : lanes_)
    if (lane->worker.joinable())
      lane->worker.join();
  return discarded;
}

void BusinessExecutor::RunLane(std::size_t index) {
  auto &lane = *lanes_[index];
  for (;;) {
    Task task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      lane.ready.wait(lock, [&] { return !accepting_ || !lane.tasks.empty(); });
      if (lane.tasks.empty())
        return;
      task = std::move(lane.tasks.front());
      lane.tasks.pop();
      --metrics_.queued;
      ++metrics_.active;
    }
    const auto started = std::chrono::steady_clock::now();
    bool failed = false;
    try {
      task.run();
    } catch (...) {
      // Protocol handlers log and clean up their own failures. This final guard
      // preserves the worker even if a task violates that contract.
      failed = true;
    }
    const auto finished = std::chrono::steady_clock::now();
    const double wait =
        std::chrono::duration<double, std::milli>(started - task.enqueued)
            .count();
    const double run =
        std::chrono::duration<double, std::milli>(finished - started).count();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      --metrics_.active;
      ++metrics_.completed;
      metrics_.failed += failed ? 1 : 0;
      metrics_.total_wait_ms += wait;
      metrics_.total_run_ms += run;
      metrics_.max_wait_ms = std::max(metrics_.max_wait_ms, wait);
      metrics_.max_run_ms = std::max(metrics_.max_run_ms, run);
      idle_.notify_all();
    }
  }
}
