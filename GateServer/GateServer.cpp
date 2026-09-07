#include <iostream>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include "AsioIOServicePool.h"
#include "CServer.h"
#include "ConfigMgr.h"
#include "Logger.h"
#include "LogicSystem.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "StatusGrpcClient.h"
#include "VerifyGrpcClient.h"
/*
主线程
↓																					主线程、主 io_context
主 io_context																├─ signal_set
├─ signal_set															└─ CServer::_acceptor
├─ acceptor																	↓ 接收到连接
├─ 客户端A socket													AsioIOServicePool
├─ 客户端B socket																├─ 工作 io_context[0] ← 工作线程0
└─ 所有 async_read / async_write 回调						│      ├─ 客户端A
																									│      └─ 客户端C
																									└─ 工作 io_context[1] ← 工作线程1
																													├─ 客户端B
																													└─ 客户端D
*/
int main()
{
	try
	{
		// 2026-08-09 修改：从 config.ini 读取并校验端口，不再写死 8090。
		auto& gCfgMgr = ConfigMgr::GetInstance();
		const std::string gatePortText = gCfgMgr["GateServer"]["Port"];
		const unsigned long gatePortValue = std::stoul(gatePortText);
		if (gatePortValue == 0 || gatePortValue > 65535)
		{
			throw std::out_of_range("GateServer port must be in range 1-65535");
		}

		// 端口号。unsigned short 是 16 位无符号整数，范围 0~65535
		// 正好对应端口的取值范围端口在 TCP 协议里就是用 16 位表示
		const unsigned short port = static_cast<unsigned short>(gatePortValue);

		// 这些组件原本都是懒加载：只有收到相应 HTTP 请求时才第一次构造，
		// 因此服务器启动后立刻退出时看不到它们的初始化和析构日志。
		// 这里在启动阶段主动取得一次实例，让依赖状态和完整生命周期都清晰可见。
		(void)LogicSystem::GetInstance();
		(void)RedisMgr::GetInstance();
		(void)MysqlMgr::GetInstance();
		(void)VerifyGrpcClient::GetInstance();
		(void)StatusGrpcClient::GetInstance();

		// 这里的 1 是并发提示，表示程序预计使用一个线程执行该 io_context 的回调
		boost::asio::io_context ioc{ 1 };
		// 2026-08-09 修改：保存工作线程池实例，程序退出时显式停止并回收线程。
		auto ioServicePool = AsioIOServicePool::GetInstance();
		// SIGINT表示中断信号，在终端运行服务器时，按Ctrl + C产生这个信号
		// SIGTERM表示程序终止信号。它通常表示：外部要求这个进程结束。
		boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
		signals.async_wait([&ioc, ioServicePool](const boost::system::error_code& error, int signal_number) {
			if (error) {
				return;
			}
			LOG_INFO("Received signal ", signal_number, ", stopping GateServer...");
			ioc.stop();
			ioServicePool->Stop();
			});
		std::shared_ptr<CServer> GateServer = std::make_shared<CServer>(ioc, port);
		GateServer->Start();
		// std::make_shared<CServer>(ioc, port)->Start();
		LOG_INFO("GateServer listening on port ", port, '.');
		ioc.run();
		// 2026-08-09 修改：即使主 io_context 因其他原因退出，也回收工作线程池。
		ioServicePool->Stop();

		// 工作线程已经全部退出，现在不会再有业务代码访问这些单例。
		// 按“业务客户端 -> 数据管理器 -> 路由 -> I/O 线程池”的倒序显式销毁，
		// 让资源释放顺序确定，并确保每条具名析构日志都能正常写入 Logger。
		StatusGrpcClient::DestroyInstance();
		VerifyGrpcClient::DestroyInstance();
		MysqlMgr::DestroyInstance();
		RedisMgr::DestroyInstance();
		LogicSystem::DestroyInstance();
		ioServicePool.reset();
		AsioIOServicePool::DestroyInstance();
	}
	catch (std::exception const& e)
	{
		LOG_ERROR("GateServer fatal error: ", e.what());
		return EXIT_FAILURE;
	}
}
