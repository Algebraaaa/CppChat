#include "ConfigMgr.h"
#include "Logger.h"
#include "StatusServiceImpl.h"

#include <boost/asio.hpp>
#include <grpcpp/grpcpp.h>

#include <csignal>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

// StatusServer 的主流程：
// 1. 读取监听地址和 ChatServer 列表；
// 2. 把 StatusServiceImpl 注册到 gRPC；
// 3. 等待 GateServer 调用 GetChatServer；
// 4. 收到 Ctrl+C 等退出信号后，安全关闭服务器。
int main() {
	try {
		LOG_INFO("StatusServer startup sequence started");

		ConfigMgr& config = ConfigMgr::GetInstance();
		const SectionInfo status_server_config = config["StatusServer"];
		const std::string host = status_server_config["Host"];
		const std::string port = status_server_config["Port"];

		if (host.empty() || port.empty()) {
			throw std::runtime_error("StatusServer Host or Port is missing in config.ini");
		}

		const std::string server_address = host + ":" + port;
		LOG_INFO("Preparing gRPC listener: address=", server_address);

		// service 是真正处理 GetChatServer 请求的对象。
		StatusServiceImpl service;

		// ServerBuilder 用来配置监听地址和需要对外提供的服务。
		grpc::ServerBuilder server_builder;
		server_builder.AddListeningPort(
			server_address,
			grpc::InsecureServerCredentials());
		server_builder.RegisterService(&service);

		// BuildAndStart() 返回 unique_ptr：服务器对象由它独占管理，函数结束时自动释放。
		std::unique_ptr<grpc::Server> grpc_server = server_builder.BuildAndStart();
		if (grpc_server == nullptr) {
			throw std::runtime_error("Failed to start StatusServer on " + server_address);
		}

		LOG_INFO("gRPC server started successfully: address=", server_address,
			", service=message.StatusService");

		// gRPC 的 Wait() 会阻塞主线程，所以用一个很小的 Asio 线程专门等待退出信号。
		boost::asio::io_context signal_io_context;
		boost::asio::signal_set shutdown_signals(signal_io_context, SIGINT, SIGTERM);

		// [&grpc_server] 表示 lambda 使用外部的 grpc_server 引用。
		shutdown_signals.async_wait(
			[&grpc_server](const boost::system::error_code& error, int signal_number) {
				if (error) {
					LOG_ERROR("Signal wait failed: code=", error.value(),
						", message=", error.message());
					return;
				}

				LOG_WARNING("Shutdown signal received: signal=", signal_number);
				grpc_server->Shutdown();
			});

		std::thread shutdown_signal_thread(
			[&signal_io_context]() {
				signal_io_context.run();
			});

		// 服务器在这里持续工作，直到 Shutdown() 被调用。
		grpc_server->Wait();
		LOG_INFO("gRPC server wait completed; stopping signal loop");

		// 先让信号事件循环停止，再等待信号线程结束，避免后台线程访问已销毁对象。
		signal_io_context.stop();
		shutdown_signal_thread.join();
		LOG_INFO("StatusServer shutdown completed");
	}
	catch (const std::exception& exception) {
		LOG_ERROR("StatusServer terminated by exception: ", exception.what());
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
