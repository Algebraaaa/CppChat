#include "LogicSystem.h"

#include <csignal>
#include <cstdlib>
#include <stdexcept>

#include "AsioIOServicePool.h"
#include "CServer.h"
#include "ChatGrpcClient.h"
#include "ChatServiceImpl.h"
#include "ConfigMgr.h"
#include "Logger.h"
#include "MediaAuthorizationService.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "StatusGrpcClient.h"
#include "UserMgr.h"

int RunChatServer()
{
  // Owners survive both normal exit and partially completed startup.
  boost::asio::io_context io_context{1};
  std::shared_ptr<AsioIOServicePool> pool;
  std::shared_ptr<LogicSystem> logic_system;
  std::shared_ptr<CServer> tcp_server;
  std::unique_ptr<ChatServiceImpl> chat_service;
  std::unique_ptr<grpc::Server> grpc_server;
  std::unique_ptr<MediaAuthorizationService> media_service;
  std::unique_ptr<grpc::Server> media_server;
  bool stopped = false;
  const auto network_grace = std::chrono::seconds(5);

  auto shutdown = [&] {
    if (stopped)
      return;
    if (tcp_server)
      tcp_server->StopAccepting();
    io_context.restart();
    io_context.poll(); // Retire canceled accepts before destroying worker contexts.
    if (logic_system)
      logic_system->BeginStop();
    if (tcp_server) {
      tcp_server->StopTimer();
      tcp_server->RemoveHealth();
    }
    if (grpc_server)
      grpc_server->Shutdown(std::chrono::system_clock::now() + network_grace);
    if (media_server)
      media_server->Shutdown(std::chrono::system_clock::now() + network_grace);
    // Active SQL has no command deadline yet; joining it safely is not a
    // promise that the ENTIRE process exits within the network grace period.
    if (logic_system) {
      logic_system->Stop();
      logic_system->ReportMetrics();
    }
    if (tcp_server) {
      tcp_server->CloseSessions(network_grace);
      tcp_server->StopCleanup(network_grace);
    }
    if (pool)
      pool->Stop();
    if (logic_system)
      logic_system->SetServer(nullptr);
    stopped = true;
    LOG_INFO("ChatServer graceful shutdown complete.");
  };

  int result = EXIT_SUCCESS;
  try {
    auto &config = ConfigMgr::GetInstance();
    const auto port_text = config["SelfServer"]["Port"];
    std::size_t consumed = 0;
    const auto port_value = std::stoul(port_text, &consumed);
    if (consumed != port_text.size() || port_value == 0 || port_value > 65535)
      throw std::out_of_range("ChatServer port must be in range 1-65535");
    const auto server_name = config["SelfServer"]["Name"];
    const auto rpc_port = config["SelfServer"]["RPCPort"];
    const auto host = config["SelfServer"]["Host"];
    if (server_name.empty() || rpc_port.empty() || host.empty())
      throw std::runtime_error("SelfServer.Name, Host and RPCPort are required");

    pool = AsioIOServicePool::GetInstance();
    logic_system = LogicSystem::GetInstance();
    tcp_server = std::make_shared<CServer>(io_context, static_cast<unsigned short>(port_value));
    logic_system->SetServer(tcp_server);
    tcp_server->Start();
    chat_service = std::make_unique<ChatServiceImpl>();
    grpc::ServerBuilder builder;
    const auto rpc_address = host + ":" + rpc_port;
    builder.AddListeningPort(rpc_address, grpc::InsecureServerCredentials());
    builder.RegisterService(chat_service.get());
    grpc_server = builder.BuildAndStart();
    if (!grpc_server)
      throw std::runtime_error("Unable to start ChatServer gRPC service at " + rpc_address);
    const auto configured_media_port = config["MediaAuthorization"]["Port"];
    const std::string media_port_text = configured_media_port.empty()
                                            ? std::to_string(std::stoul(rpc_port) + 5)
                                            : configured_media_port;
    consumed = 0;
    const auto media_port = std::stoul(media_port_text, &consumed);
    if (consumed != media_port_text.size() || media_port == 0 || media_port > 65535)
      throw std::runtime_error("MediaAuthorization.Port must be in range 1-65535");
    media_service = std::make_unique<MediaAuthorizationService>();
    grpc::ServerBuilder media_builder;
    const auto media_address = "127.0.0.1:" + std::to_string(media_port);
    media_builder.AddListeningPort(media_address, grpc::InsecureServerCredentials());
    media_builder.SetMaxReceiveMessageSize(1024);
    media_builder.SetMaxSendMessageSize(1024);
    media_builder.SetCompressionAlgorithmSupportStatus(GRPC_COMPRESS_GZIP, false);
    media_builder.SetCompressionAlgorithmSupportStatus(GRPC_COMPRESS_DEFLATE, false);
    media_builder.AddChannelArgument("grpc.max_concurrent_streams", 4);
    media_builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MIN_POLLERS, 1);
    media_builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MAX_POLLERS, 4);
    media_builder.RegisterService(media_service.get());
    media_server = media_builder.BuildAndStart();
    if (!media_server)
      throw std::runtime_error("Unable to start loopback media authorization at " + media_address);
    LOG_INFO("Group media authorization listening at ", media_address, '.');
    if (!tcp_server->PublishHealth())
      throw std::runtime_error("Cannot register chat node; check Redis or duplicate node name.");
    LOG_INFO("ChatServer gRPC service listening at ", rpc_address, '.');

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code &error, int signal_number) {
      if (error)
        return;
      LOG_INFO("Received signal ", signal_number, ", stopping ChatServer...");
      tcp_server->StopAccepting();
      logic_system->BeginStop();
      io_context.stop();
    });
    LOG_INFO("ChatServer listening on port ", port_value, '.');
    io_context.run();
    shutdown();
  } catch (const std::exception &error) {
    LOG_ERROR("ChatServer fatal error: ", error.what());
    shutdown();
    result = EXIT_FAILURE;
  }

  media_server.reset();
  media_service.reset();
  grpc_server.reset();
  chat_service.reset();
  tcp_server.reset();
  logic_system.reset();
  LogicSystem::DestroyInstance();
  ChatGrpcClient::DestroyInstance();
  UserMgr::DestroyInstance();
  MysqlMgr::DestroyInstance();
  RedisMgr::DestroyInstance();
  StatusGrpcClient::DestroyInstance();
  pool.reset();
  AsioIOServicePool::DestroyInstance();
  return result;
}
