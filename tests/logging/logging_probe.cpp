#include "MetricsLogPolicy.h"
#include "logging/ServerLogger.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

void Require(bool condition)
{
  if (!condition)
    throw std::runtime_error("Logging policy assertion failed");
}

void CheckMetrics()
{
  using Clock = MetricsLogPolicy::Clock;
  using Decision = MetricsLogPolicy::Decision;
  const auto start = Clock::time_point{};
  MetricsLogPolicy policy(start);
  BusinessMetrics metrics;
  Require(policy.Check(metrics, start + std::chrono::seconds(200)) == Decision::Quiet);
  metrics.completed = 1;
  Require(policy.Check(metrics, start + std::chrono::seconds(299)) == Decision::Quiet);
  Require(policy.Check(metrics, start + std::chrono::seconds(300)) == Decision::Summary);
  Require(policy.Check(metrics, start + std::chrono::seconds(600)) == Decision::Quiet);
  metrics.failed = 1;
  Require(policy.Check(metrics, start + std::chrono::seconds(601)) == Decision::Warning);
  Require(policy.Check(metrics, start + std::chrono::seconds(602)) == Decision::Quiet);
  metrics.rejected = 2;
  Require(policy.Check(metrics, start + std::chrono::seconds(603)) == Decision::Quiet);
  Require(policy.Check(metrics, start + std::chrono::seconds(661)) == Decision::Warning);
  Require(policy.Check(metrics, start + std::chrono::seconds(999)) == Decision::Quiet);
  metrics.completed++;
  Require(policy.Check(metrics, start + std::chrono::seconds(1000), true) == Decision::Summary);
  Require(policy.Check(metrics, start + std::chrono::seconds(1001), true) == Decision::Quiet);
  policy.SetInterval(std::chrono::seconds(30));
  metrics.active = 1;
  Require(policy.Check(metrics, start + std::chrono::seconds(1030)) == Decision::Summary);
  metrics.active = 0;
  Require(policy.Check(metrics, start + std::chrono::seconds(1060)) == Decision::Summary);
  Require(policy.Check(metrics, start + std::chrono::seconds(1090)) == Decision::Quiet);
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
#else
int main(int argc, char **argv)
#endif
{
  try {
    if (argc != 3)
      return 2;
    const std::string mode = std::filesystem::path(argv[1]).u8string();
    if (mode == "metrics") {
      CheckMetrics();
      return 0;
    }
    auto options = ServerLogging::Options::FromEnvironment();
    options.directory = std::filesystem::path(argv[2]);
    if (mode == "rotate") {
      options.max_bytes = 1024;
      options.backups = 2;
    }
    if (mode == "overflow")
      options.queue_size = 1;
    ServerLogging::Logger logger("Probe", options);
    std::cout << "PATH=" << logger.FilePath().u8string() << std::endl;
    if (mode == "hold") {
      logger.Warning("OWNER_READY");
      std::string input;
      std::getline(std::cin, input);
      logger.Warning("OWNER_DONE");
    } else if (mode == "rotate") {
      for (unsigned i = 0; i < 100; ++i)
        logger.Warning("ROTATE-", i, '-', std::string(120, 'x'));
    } else if (mode == "threaded") {
      std::vector<std::thread> workers;
      for (unsigned thread = 0; thread < 4; ++thread)
        workers.emplace_back([&, thread] {
          for (unsigned i = 0; i < 200; ++i)
            logger.Info("RECORD-", thread, '-', i);
        });
      for (auto &worker : workers)
        worker.join();
      logger.Warning("IMPORTANT_END");
    } else if (mode == "overflow") {
      for (unsigned i = 0; i < 100000; ++i)
        logger.Info("FLOOD-", i);
      logger.Error("IMPORTANT_SURVIVES");
    } else {
      logger.Debug("DEBUG_RECORD");
      logger.Info("INFO_RECORD {literal braces}");
      logger.Warning("WARNING_RECORD");
      logger.Error("ERROR_RECORD");
      logger.SetMinimumLevel(ServerLogging::Level::Error);
      logger.Info("FILTERED_RECORD");
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
