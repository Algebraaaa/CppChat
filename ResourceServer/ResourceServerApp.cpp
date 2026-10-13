#include "ResourceServerApp.h"
#include "BusinessExecutor.h"
#include "Logger.h"
#include "ResourceConfig.h"
#include "ResourceProtocol.h"
#include "ResourceStore.h"
#include <array>
#include <boost/asio.hpp>
#include <csignal>
#include <functional>
#include <json/json.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
namespace asio = boost::asio;
using tcp = asio::ip::tcp;
namespace cppchat::resource {
namespace {
class ResourceSession : public std::enable_shared_from_this<ResourceSession> {
public:
  ResourceSession(tcp::socket socket, ResourceStore &store, BusinessExecutor &workers,
                  std::string key, int timeout, std::function<void()> onClose)
      : socket_(std::move(socket)), timer_(socket_.get_executor()), store_(store),
        workers_(workers), key_(std::move(key)), timeout_(timeout), onClose_(std::move(onClose))
  {
  }
  void Start() { ReadHead(); }
  void Close()
  {
    if (closed_)
      return;
    closed_ = true;
    timer_.cancel();
    boost::system::error_code ignored;
    socket_.close(ignored);
    onClose_();
  }

private:
  void ArmTimeout()
  {
    timer_.expires_after(std::chrono::seconds(timeout_));
    auto self = shared_from_this();
    timer_.async_wait([self](const boost::system::error_code &error) {
      if (!error)
        self->Close();
    });
  }
  void ReadHead()
  {
    ArmTimeout();
    auto self = shared_from_this();
    asio::async_read(
        socket_, asio::buffer(head_), [self](const boost::system::error_code &error, std::size_t) {
          if (error) {
            self->Close();
            return;
          }
          const std::uint16_t id =
              static_cast<std::uint16_t>((self->head_[0] << 8) | self->head_[1]);
          const std::size_t length = (self->head_[2] << 8) | self->head_[3];
          if (length == 0 || length > kMaximumPacketSize ||
              (id != kUploadInitRequest && id != kUploadChunkRequest && id != kDownloadRequest)) {
            self->Close();
            return;
          }
          self->body_.assign(length, '\0');
          self->ReadBody(id);
        });
  }
  void ReadBody(std::uint16_t id)
  {
    ArmTimeout();
    auto self = shared_from_this();
    asio::async_read(socket_, asio::buffer(body_),
                     [self, id](const boost::system::error_code &error, std::size_t) {
                       if (error) {
                         self->Close();
                         return;
                       }
                       self->Dispatch(id);
                     });
  }
  void Dispatch(std::uint16_t id)
  {
    ArmTimeout();
    auto self = shared_from_this();
    if (!workers_.Submit(key_, [self, id, body = std::move(body_)] {
          Json::Value reply;
          try {
            Json::CharReaderBuilder builder;
            std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
            Json::Value request;
            std::string errors;
            reply = reader->parse(body.data(), body.data() + body.size(), &request, &errors) &&
                            request.isObject()
                        ? self->store_.Handle(id, request)
                        : Error("invalid JSON");
          } catch (const std::exception &error) {
            LOG_WARNING("Resource handler failed: ", error.what());
            reply = Error("resource operation failed");
          }
          asio::post(self->socket_.get_executor(), [self, id, reply = std::move(reply)] {
            if (!self->closed_)
              self->WriteReply(id, reply);
          });
        }))
      WriteReply(id, Error("server busy; retry later"));
  }
  void WriteReply(std::uint16_t id, const Json::Value &reply)
  {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    const auto body = Json::writeString(writer, reply);
    if (body.size() > UINT16_MAX) {
      Close();
      return;
    }
    const std::uint16_t response = static_cast<std::uint16_t>(id + 1);
    output_.clear();
    output_.push_back(static_cast<char>(response >> 8));
    output_.push_back(static_cast<char>(response & 255));
    output_.push_back(static_cast<char>(body.size() >> 8));
    output_.push_back(static_cast<char>(body.size() & 255));
    output_ += body;
    ArmTimeout();
    auto self = shared_from_this();
    asio::async_write(socket_, asio::buffer(output_),
                      [self](const boost::system::error_code &error, std::size_t) {
                        if (error)
                          self->Close();
                        else if (!self->closed_)
                          self->ReadHead();
                      });
  }
  tcp::socket socket_;
  asio::steady_timer timer_;
  ResourceStore &store_;
  BusinessExecutor &workers_;
  std::string key_;
  int timeout_;
  std::function<void()> onClose_;
  bool closed_ = false; // Socket state is accessed only by the single I/O loop.
  std::array<unsigned char, 4> head_{};
  std::string body_, output_;
};

class ResourceListener {
public:
  ResourceListener(asio::io_context &io, const Settings &settings, ResourceStore &store,
                   BusinessExecutor &workers)
      : acceptor_(io, tcp::endpoint(asio::ip::make_address(settings.listen), settings.port)),
        settings_(settings), store_(store), workers_(workers)
  {
    Accept();
  }
  ~ResourceListener() { Stop(); }
  void Stop()
  {
    boost::system::error_code ignored;
    acceptor_.close(ignored);
    auto sessions = sessions_;
    for (auto &entry : sessions)
      entry.second->Close();
  }

private:
  void Accept()
  {
    acceptor_.async_accept([this](const boost::system::error_code &error, tcp::socket socket) {
      if (!error) {
        if (sessions_.size() >= static_cast<std::size_t>(settings_.maxConnections)) {
          LOG_WARNING("Resource connection limit reached");
          boost::system::error_code ignored;
          socket.close(ignored);
        } else {
          const auto key = std::to_string(++sequence_);
          auto session = std::make_shared<ResourceSession>(std::move(socket), store_, workers_, key,
                                                           settings_.timeoutSeconds,
                                                           [this, key] { sessions_.erase(key); });
          sessions_[key] = session;
          session->Start();
        }
      } else if (error != asio::error::operation_aborted) {
        LOG_WARNING("Resource accept failed: ", error.message());
      }
      if (acceptor_.is_open())
        Accept();
    });
  }
  tcp::acceptor acceptor_;
  Settings settings_;
  ResourceStore &store_;
  BusinessExecutor &workers_;
  std::uint64_t sequence_ = 0;
  std::unordered_map<std::string, std::shared_ptr<ResourceSession>> sessions_;
};

} // namespace

int RunResourceServer()
{
  try {
    Settings settings;
    asio::io_context io;
    ResourceStore store(settings);
    BusinessExecutor workers(settings.workers, settings.queueLimit);
    ResourceListener listener(io, settings, store, workers);
    asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code &error, int) {
      if (!error) {
        listener.Stop();
        io.stop();
      }
    });
    LOG_INFO("ResourceServer listening on ", settings.listen, ':', settings.port,
             ", workers=", settings.workers, ", max_connections=", settings.maxConnections);
    io.run();
    listener.Stop();
    workers.Stop();
    const auto m = workers.Snapshot();
    LOG_INFO("Resource metrics: completed=", m.completed, ", rejected=", m.rejected,
             ", failed=", m.failed, ", max_wait_ms=", m.max_wait_ms);
    return 0;
  } catch (const std::exception &error) {
    LOG_ERROR("ResourceServer failed: ", error.what());
    return 1;
  }
}

} // namespace cppchat::resource
