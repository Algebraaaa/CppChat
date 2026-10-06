#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

struct BusinessMetrics {
  std::size_t queued = 0;
  std::size_t active = 0;
  std::size_t high_water = 0;
  std::uint64_t completed = 0;
  std::uint64_t rejected = 0;
  std::uint64_t failed = 0;
  double total_wait_ms = 0;
  double total_run_ms = 0;
  double max_wait_ms = 0;
  double max_run_ms = 0;
};

// Immutable connection keys select a FIFO lane. Login must not change the key.
// Only the lifecycle owner calls Stop(); tasks must not stop their own
// executor.
class BusinessExecutor final {
public:
  BusinessExecutor(std::size_t workers, std::size_t queue_limit);
  ~BusinessExecutor();
  BusinessExecutor(const BusinessExecutor &) = delete;
  BusinessExecutor &operator=(const BusinessExecutor &) = delete;

  bool Submit(const std::string &connection_key, std::function<void()> task);
  BusinessMetrics Snapshot() const;
  void BeginStop(); // Nonblocking admission cutoff; accepted tasks still run.
  bool WaitIdleUntil(std::chrono::steady_clock::time_point deadline);
  // drain=false discards WAITING tasks, never interrupts an active task.
  std::size_t Stop(bool drain = true);

private:
  struct Task {
    std::function<void()> run;
    std::chrono::steady_clock::time_point enqueued;
  };
  struct Lane {
    std::queue<Task> tasks;
    std::condition_variable ready;
    std::thread worker;
  };
  void RunLane(std::size_t index);

  const std::size_t queue_limit_;
  std::vector<std::unique_ptr<Lane>> lanes_;
  mutable std::mutex mutex_;
  std::mutex stop_mutex_;
  std::condition_variable idle_;
  bool accepting_ = true;
  BusinessMetrics metrics_;
};
