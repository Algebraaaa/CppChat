#include "CServer.h"
#include "AsioIOServicePool.h"
#include "ConfigMgr.h"
#include "HttpConnection.h"
#include "Logger.h"
#include <boost/system/error_code.hpp>
#include <future>
#include <stdexcept>
// 对于类的成员是引用类型，需要通过初始化列表进行初始化
CServer::CServer(boost::asio::io_context &ioc, unsigned short port)
    : _acceptor(ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(),
                                                    port)),
      _ioc(ioc) {
  try {
    const auto text =
        ConfigMgr::GetInstance()["Business"].GetValue("MaxConnections", "1024");
    std::size_t consumed = 0;
    const auto limit = std::stoul(text, &consumed);
    if (consumed != text.size() || limit < 1 || limit > 100000)
      throw std::out_of_range("MaxConnections");
    max_connections_ = limit;
  } catch (const std::exception &) {
    LOG_WARNING("Invalid Business.MaxConnections; using 1024.");
  }
}

void CServer::Start() {
  if (stopping_ || !_acceptor.is_open())
    return;
  auto owner = weak_from_this();

  /*
   * 旧版流程：
   * 1. CServer 用主 io_context 创建 _socket；
   * 2. accept 成功后，再用 std::move(_socket) 把 socket 交给 HttpConnection。
   *
   * std::move 只转移 socket 的所有权，不会更换 socket 内部绑定的 executor。
   * 因此旧版 socket 即使被移动到 HttpConnection，后续异步读写仍由主 io_context
   * 调度， 并没有真正把连接分配给工作线程。
   *
   * 当前流程：
   * 1. 先从 AsioIOServicePool 选择一个工作 io_context；
   * 2. HttpConnection 直接使用这个工作 io_context 创建自己的 socket；
   * 3. acceptor 把新连接接收到这个 socket 中。
   *
   * 这样 socket 从出生时就属于工作 io_context，后续 async_read/async_write
   * 才会由对应的工作线程执行，这才是真正的线程分流。
   */
  auto &io_context = AsioIOServicePool::GetInstance()->GetIOService();
  std::shared_ptr<HttpConnection> new_con =
      std::make_shared<HttpConnection>(io_context);
  _acceptor.async_accept(
      new_con->Getsocket(), [owner, new_con](boost::system::error_code ec) {
        auto self = owner.lock();
        if (!self)
          return;
        try {
          // 出错则放弃这个连接，继续监听新链接
          if (ec) {
            self->Start();
            return;
          }
          bool admitted = false;
          {
            std::lock_guard<std::mutex> lock(self->mutex_);
            if (!self->stopping_ &&
                self->connections_.size() < self->max_connections_) {
              self->connections_.emplace(new_con->GetId(), new_con);
              admitted = true;
            }
          }
          if (!admitted) {
            LOG_WARNING("Gateway connection limit reached: limit=",
                        self->max_connections_);
            new_con->Abort();
            self->Start();
            return;
          }
          std::weak_ptr<CServer> owner = self;
          new_con->SetCloseCallback([owner, id = new_con->GetId()] {
            if (auto server = owner.lock())
              server->RemoveConnection(id);
          });
          // new_con 从创建时就拥有这个 socket，不再需要从 CServer 移动 socket。
          // 开始异步读取这个客户端发来的 HTTP 请求。
          new_con->Start();
          // 继续监听
          self->Start();
        }
        // 接住异常对象，并把它命名为 exp
        catch (std::exception &exp) {
          LOG_ERROR("CServer accept handler exception: ", exp.what());
          self->Start();
        }
      });
}

void CServer::Stop() {
  stopping_ = true;
  boost::system::error_code ignored;
  _acceptor.cancel(ignored);
  _acceptor.close(ignored);
  for (const auto &connection : SnapshotConnections())
    connection->Quiesce();
}

void CServer::RemoveConnection(const std::string &id) {
  std::lock_guard<std::mutex> lock(mutex_);
  connections_.erase(id);
}

std::vector<std::shared_ptr<HttpConnection>> CServer::SnapshotConnections() {
  std::vector<std::shared_ptr<HttpConnection>> connections;
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &entry : connections_)
    connections.push_back(entry.second);
  return connections;
}

void CServer::CloseConnections(std::chrono::seconds timeout) {
  std::vector<std::future<void>> completed;
  for (const auto &connection : SnapshotConnections()) {
    auto completion = std::make_shared<std::promise<void>>();
    completed.push_back(completion->get_future());
    connection->DrainAndClose(timeout,
                              [completion] { completion->set_value(); });
  }
  for (auto &completion : completed)
    completion.get();
}
