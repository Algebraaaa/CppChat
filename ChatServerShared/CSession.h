#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <ctime>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include <boost/asio.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "MsgNode.h"
#include "const.h"

class CServer;
class LogicSystem;

class CSession : public std::enable_shared_from_this<CSession> {
public:
  CSession(boost::asio::io_context &io_context, std::weak_ptr<CServer> server);
  ~CSession();

  boost::asio::ip::tcp::socket &GetSocket();
  const std::string &GetUuid() const;
  const std::string &GetSessionId() const;
  bool BindAuthenticatedUser(int uid);
  int GetUserId() const;
  // A retiring session may still flush its final notice, but cannot start work.
  bool IsClosed() const { return closed_.load() || retiring_.load(); }

  void Start();
  void Send(const char *message, short message_length, short message_id);
  void Send(const std::string &message, short message_id);
  // on_closed runs on the connection strand after the socket has closed.
  void Close(std::function<void()> on_closed = {});
  void DrainAndClose(std::chrono::seconds timeout,
                     std::function<void()> on_closed);
  void NotifyOffline(int uid);
  bool IsHeartbeatExpired(std::time_t now) const;
  void UpdateHeartbeat();

private:
  void
  DealExceptionSession(); // Only after physical close on the owning strand.
  using ReadHandler =
      std::function<void(const boost::system::error_code &, std::size_t)>;

  void AsyncReadBody(std::size_t total_length);
  void AsyncReadHead();
  void AsyncReadFull(std::size_t total_length, ReadHandler handler);
  void AsyncReadLength(std::size_t read_length, std::size_t total_length,
                       ReadHandler handler);
  void HandleWrite(const boost::system::error_code &error);
  bool QueueSend(std::shared_ptr<SendNode> message, bool close_after_write);
  void StartWrite();
  void CloseOnExecutor();
  void ArmDrainDeadline(std::chrono::seconds timeout);
  void HandleReadFailure(const char *stage,
                         const boost::system::error_code &error);

  boost::asio::strand<boost::asio::io_context::executor_type> strand_;
  boost::asio::ip::tcp::socket socket_;
  std::string uuid_;
  std::array<char, MAX_LENGTH> data_{};
  std::weak_ptr<CServer> server_;
  boost::asio::steady_timer drain_timer_;
  bool drain_timer_active_ = false;
  std::vector<std::function<void()>> close_handlers_;
  std::atomic_bool closed_{false};
  std::atomic_bool retiring_{false};
  std::atomic_bool cleanup_started_{false};
  std::atomic_int user_id_{0};
  // Serialize publishing a login with taking the cleanup UID snapshot.
  std::mutex login_mutex_;
  std::atomic<std::time_t> last_heartbeat_;
  // send_queue_ 是“当前客户端连接的待发送消息队列”
  // 1、解耦合，削峰，2、保证发送的顺序 3、多线程安全
  std::queue<std::shared_ptr<SendNode>> send_queue_;
  std::mutex send_mutex_;
  bool close_after_write_ = false;
  // Only the strand reads/writes this flag. Queue admission remains bounded
  // under send_mutex_ before scheduling any executor work.
  bool write_active_ = false;
  std::shared_ptr<RecvNode> receive_message_node_;
  std::shared_ptr<MsgNode> receive_header_node_;
};

class LogicNode {
  friend class LogicSystem;

public:
  LogicNode(std::shared_ptr<CSession> session,
            std::shared_ptr<RecvNode> receive_node);

private:
  std::shared_ptr<CSession> _session;
  std::shared_ptr<RecvNode> _recvnode;
};
