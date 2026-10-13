#pragma once
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

class HttpConnection;
class CServer : public std::enable_shared_from_this<CServer>
{
  public:
    // 2026-08-09 修改：端口是小型只读数值，按值传递更直接。
    CServer(boost::asio::io_context &ioc, unsigned short port, const std::string &host = "0.0.0.0");
    void Start();
    void Stop(); // Main acceptor executor only.
    void CloseConnections(std::chrono::seconds timeout);

  private:
    void RemoveConnection(const std::string &id);
    std::vector<std::shared_ptr<HttpConnection>> SnapshotConnections();
    std::mutex mutex_;
    std::map<std::string, std::shared_ptr<HttpConnection>> connections_;
    std::size_t max_connections_ = 1024;
    std::atomic_bool stopping_{false};
    boost::asio::ip::tcp::acceptor _acceptor; // 监听器
    boost::asio::io_context &_ioc;            // 事件循环的引用
                                              /*
                                              CServer 负责监听、连接准入和停机时的连接生命周期：
                                              CServer
                                              ├─ 保存 acceptor
                                              ├─ 监听新连接
                                              └─ 把连接交给 HttpConnection
                                              各连接的 socket 和读写仍由 HttpConnection 独立持有。
                                               tcp::socket _socket;
                                               */
};
