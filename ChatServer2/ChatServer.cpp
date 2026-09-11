#include "LogicSystem.h"

#include <cstdlib>
#include <csignal>
#include <stdexcept>
#include <thread>

#include "AsioIOServicePool.h"
#include "CServer.h"
#include "ChatGrpcClient.h"
#include "ChatServiceImpl.h"
#include "ConfigMgr.h"
#include "Logger.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "StatusGrpcClient.h"
#include "UserMgr.h"

int main()
{
	try
	{
		auto& config = ConfigMgr::GetInstance();
		const std::string port_text = config["SelfServer"]["Port"];
		if (port_text.empty())
		{
			throw std::runtime_error("SelfServer.Port is missing in config.ini");
		}

		const unsigned long port_value = std::stoul(port_text);
		if (port_value == 0 || port_value > 65535)
		{
			throw std::out_of_range("ChatServer2 port must be in range 1-65535");
		}

		auto pool = AsioIOServicePool::GetInstance();
		auto logic_system = LogicSystem::GetInstance();
		boost::asio::io_context io_context{ 1 };
		{
			const std::string server_name = config["SelfServer"]["Name"];
			const std::string rpc_port = config["SelfServer"]["RPCPort"];
			const std::string host = config["SelfServer"]["Host"];
			if (server_name.empty() || rpc_port.empty() || host.empty())
			{
				throw std::runtime_error(
					"SelfServer.Name, Host and RPCPort are required in config.ini");
			}

			RedisMgr::GetInstance()->HSet(LOGIN_COUNT, server_name, "0");
			Defer remove_online_count([&server_name]()
			{
				RedisMgr::GetInstance()->HDel(LOGIN_COUNT, server_name);
			});

			const auto port = static_cast<unsigned short>(port_value);
			auto tcp_server = std::make_shared<CServer>(io_context, port);
			logic_system->SetServer(tcp_server);
			tcp_server->StartTimer();

			ChatServiceImpl chat_service;
			grpc::ServerBuilder builder;
			const std::string rpc_address = host + ":" + rpc_port;
			builder.AddListeningPort(
				rpc_address, grpc::InsecureServerCredentials());
			builder.RegisterService(&chat_service);
			auto grpc_server = builder.BuildAndStart();
			if (!grpc_server)
			{
				throw std::runtime_error(
					"Unable to start ChatServer2 gRPC service at " + rpc_address);
			}
			LOG_INFO("ChatServer2 gRPC service listening at ", rpc_address, '.');
			std::thread grpc_thread([&grpc_server]()
			{
				grpc_server->Wait();
			});

			boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
			signals.async_wait([&io_context, pool, &grpc_server](
				const boost::system::error_code& error,
				int signal_number)
			{
				if (error)
				{
					return;
				}
				LOG_INFO("Received signal ", signal_number, ", stopping ChatServer2...");
				io_context.stop();
				pool->Stop();
				grpc_server->Shutdown();
			});

			LOG_INFO("ChatServer2 listening on port ", port, '.');
			try
			{
				io_context.run();
			}
			catch (...)
			{
				grpc_server->Shutdown();
				if (grpc_thread.joinable())
				{
					grpc_thread.join();
				}
				throw;
			}
			grpc_server->Shutdown();
			if (grpc_thread.joinable())
			{
				grpc_thread.join();
			}
			tcp_server->StopTimer();
			logic_system->SetServer(nullptr);
			tcp_server.reset();
			pool->Stop();
		}

		// 先停止逻辑线程，避免它在 StatusGrpcClient 销毁后继续发起 RPC。
		logic_system.reset();
		LogicSystem::DestroyInstance();
		ChatGrpcClient::DestroyInstance();
		UserMgr::DestroyInstance();
		MysqlMgr::DestroyInstance();
		RedisMgr::DestroyInstance();
		StatusGrpcClient::DestroyInstance();
		pool.reset();
		AsioIOServicePool::DestroyInstance();
	}
	catch (const std::exception& exception)
	{
		LOG_ERROR("ChatServer2 fatal error: ", exception.what());
		LogicSystem::DestroyInstance();
		ChatGrpcClient::DestroyInstance();
		UserMgr::DestroyInstance();
		MysqlMgr::DestroyInstance();
		RedisMgr::DestroyInstance();
		StatusGrpcClient::DestroyInstance();
		AsioIOServicePool::DestroyInstance();
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
