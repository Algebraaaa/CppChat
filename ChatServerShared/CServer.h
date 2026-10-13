#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <boost/asio.hpp>

#include "BusinessExecutor.h"
#include "CSession.h"

class CServer : public std::enable_shared_from_this<CServer> {
public:
  CServer(boost::asio::io_context &io_context, unsigned short port);
  ~CServer();
  void Start();
  void StopAccepting(); // Acceptor/lifecycle owner only.
  bool IsStopping() const { return stopping_.load(); }
  void CleanupSession(const std::string &uuid, int uid);
  void StopCleanup(std::chrono::seconds timeout);

  void ClearSession(const std::string &uuid);
  std::shared_ptr<CSession> GetSession(const std::string &uuid);
  bool CheckValid(const std::string &uuid);
  void StartTimer();
  void StopTimer();
  bool PublishHealth(std::size_t online_count = 0);
  void RemoveHealth();
  // Lifecycle owner only: business workers stopped, connection I/O still alive.
  void CloseSessions(std::chrono::seconds timeout = std::chrono::seconds(5));

private:
  void HandleAccept(std::shared_ptr<CSession> session, const boost::system::error_code &error);
  void StartAccept();
  void HandleTimer(const boost::system::error_code &error);
  void MaintainSessions(const std::vector<std::shared_ptr<CSession>> &sessions);

  boost::asio::io_context &io_context_;
  unsigned short port_;
  boost::asio::ip::tcp::acceptor acceptor_;
  std::map<std::string, std::shared_ptr<CSession>> sessions_;
  std::mutex mutex_;
  boost::asio::steady_timer heartbeat_timer_;
  std::string instance_id_;
  BusinessExecutor maintenance_{1, 1};
  // Closed sessions stay in sessions_ until cleanup completes, so the
  // connection admission cap also bounds pending credential cleanup.
  BusinessExecutor cleanup_{2, 100000};
  std::atomic_bool stopping_{false};
  std::size_t max_connections_ = 4096;
};
