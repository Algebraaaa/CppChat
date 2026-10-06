#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
/*
客户端发送：
服务器返回： POST /get_verifycode HTTP/1.1
HTTP/1.1 200 OK Host: 127.0.0.1 : 8080
Server: GateServer Content - Type : application / json
Content-Type: application/json Content - Length : 26
Content-Length: 38
{"email":"123@qq.com"}
{"error":0,"email":"123@qq.com"} _request
_response ├─ method       POST
├─ version      HTTP/1.1 ├─ target       /get_verifycode
├─ result       200 ├─ version      HTTP/1.1
├─ reason       OK ├─ fields       Host、Content-Type、Content-Length
├─ fields       Server、Content-Type、Content-Length └─ body
{"email":"123@qq.com"} └─ body         {"error":0,"email":"123@qq.com"}
*/
class HttpConnection : public std::enable_shared_from_this<HttpConnection> {
  friend class LogicSystem;

public:
  HttpConnection(boost::asio::io_context &ioc);
  void Start();
  const std::string &GetId() const { return connection_key_; }
  void SetCloseCallback(std::function<void()> callback) {
    on_closed_ = std::move(callback);
  }
  void Quiesce();
  void Abort();
  void DrainAndClose(std::chrono::seconds timeout,
                     std::function<void()> callback);
  boost::asio::ip::tcp::socket &Getsocket() { return _socket; }
  void PreParseGetParam();

private:
  // 超时检测函数
  void CheckDeadline();
  // 应答函数
  void WriteResponse();
  // 解析数据函数
  void HandleReq();
  void SendError(boost::beast::http::status status, const char *message);
  void Close(); // Only called on strand_.
  void FinishResponse();
  void DrainInput();
  boost::asio::strand<boost::asio::io_context::executor_type> strand_;
  boost::asio::ip::tcp::socket _socket;
  // _buffer 是 HTTP 读取过程中的临时中转站，最多允许暂存约 8 KB
  // 的未处理网络数据
  boost::beast::flat_buffer _buffer{8192};
  boost::beast::http::request_parser<boost::beast::http::dynamic_body> parser_;
  // 客户端发给服务器的 HTTP 请求
  boost::beast::http::request<boost::beast::http::dynamic_body> _request;
  // 服务器准备发给客户端的 HTTP 响应
  boost::beast::http::response<boost::beast::http::dynamic_body> _response;
  // 2026-08-09 修改：这里只构造计时器；连接建立并执行 Start() 后才开始倒计时。
  // 处理该连接的超时计时器。
  // steady 表示它使用稳定的单调时钟计时
  // 即使用户修改了电脑系统时间，也不会轻易影响倒计时。
  boost::asio::steady_timer deadline_;
  std::atomic_bool cancelled_{false};
  std::atomic_bool accepting_requests_{true};
  bool request_submitted_ = false;
  bool response_started_ = false;
  bool socket_closed_ = false;
  std::function<void()> on_closed_;
  std::vector<std::function<void()>> drain_handlers_;
  const std::string connection_key_;
  unsigned int timeout_seconds_ = 60;
  std::array<char, 4096> drain_buffer_{};
  std::size_t drain_remaining_ = 64 * 1024;
  // After read completion the business task exclusively builds _response.
  // Its final post transfers response ownership back to the I/O strand.

  std::string _get_url;
  std::unordered_map<std::string, std::string> _get_params;
};
