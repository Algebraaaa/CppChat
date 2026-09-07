#include "LogicSystem.h"

#include <cstdlib>
#include <csignal>
#include <stdexcept>

#include "AsioIOServicePool.h"
#include "CServer.h"
#include "ConfigMgr.h"
#include "Logger.h"
#include "StatusGrpcClient.h"

int main()
{
	try
	{
		auto& config = ConfigMgr::Inst();
		const std::string port_text = config["SelfServer"]["Port"];
		if (port_text.empty())
		{
			throw std::runtime_error("SelfServer.Port is missing in config.ini");
		}

		const unsigned long port_value = std::stoul(port_text);
		if (port_value == 0 || port_value > 65535)
		{
			throw std::out_of_range("ChatServer port must be in range 1-65535");
		}

		auto pool = AsioIOServicePool::GetInstance();
		auto logic_system = LogicSystem::GetInstance();
		boost::asio::io_context io_context{ 1 };
		{
			boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
			signals.async_wait([&io_context, pool](
				const boost::system::error_code& error,
				int signal_number)
			{
				if (error)
				{
					return;
				}
				LOG_INFO("Received signal ", signal_number, ", stopping ChatServer...");
				io_context.stop();
				pool->Stop();
			});

			const auto port = static_cast<unsigned short>(port_value);
			CServer server(io_context, port);
			LOG_INFO("ChatServer listening on port ", port, '.');
			io_context.run();
			pool->Stop();
		}

		// 先停止逻辑线程，避免它在 StatusGrpcClient 销毁后继续发起 RPC。
		logic_system.reset();
		LogicSystem::DestroyInstance();
		StatusGrpcClient::DestroyInstance();
		pool.reset();
		AsioIOServicePool::DestroyInstance();
	}
	catch (const std::exception& exception)
	{
		LOG_ERROR("ChatServer fatal error: ", exception.what());
		LogicSystem::DestroyInstance();
		StatusGrpcClient::DestroyInstance();
		AsioIOServicePool::DestroyInstance();
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
