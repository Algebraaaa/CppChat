#include <csignal>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

#include "AsioIOServicePool.h"
#include "CServer.h"
#include "ConfigMgr.h"
#include "Logger.h"
#include "LogicSystem.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "StatusGrpcClient.h"
#include "VerifyGrpcClient.h"
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

int main()
{
    // These owners outlive shutdown, including partially completed startup.
    boost::asio::io_context ioc{1};
    std::shared_ptr<LogicSystem> logic;
    std::shared_ptr<AsioIOServicePool> pool;
    std::shared_ptr<CServer> server;
    bool stopped = false;
    auto shutdown = [&]
    {
        if (stopped)
            return;
        if (server)
            server->Stop();
        ioc.restart();
        ioc.poll(); // Release the canceled accept's socket before its I/O pool.
        if (logic)
        {
            logic->BeginStop();
            // SQL already executing cannot be safely interrupted here.
            logic->Stop();
        }
        // I/O workers remain alive until accepted responses finish or expire.
        if (server)
            server->CloseConnections(std::chrono::seconds(5));
        if (pool)
            pool->Stop();
        stopped = true;
        LOG_INFO("GateServer graceful shutdown complete.");
    };

    int result = EXIT_SUCCESS;
    try
    {
        auto &config = ConfigMgr::GetInstance();
        const std::string text = config["GateServer"]["Port"];
        std::size_t consumed = 0;
        const auto port = std::stoul(text, &consumed);
        if (consumed != text.size() || port == 0 || port > 65535)
            throw std::out_of_range("GateServer port must be in range 1-65535");

        logic = LogicSystem::GetInstance();
        (void)RedisMgr::GetInstance();
        (void)MysqlMgr::GetInstance();
        (void)VerifyGrpcClient::GetInstance();
        (void)StatusGrpcClient::GetInstance();
        pool = AsioIOServicePool::GetInstance();
        const auto host = config["GateServer"].GetValue("Host", "0.0.0.0");
        server = std::make_shared<CServer>(ioc, static_cast<unsigned short>(port), host);
        server->Start();
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait(
            [&](const boost::system::error_code &error, int signal_number)
            {
                if (error)
                    return;
                LOG_INFO("Received signal ", signal_number, ", stopping GateServer...");
                server->Stop();
                logic->BeginStop();
                ioc.stop();
            });
        LOG_INFO("GateServer listening on port ", port, '.');
        ioc.run();
        shutdown();
    }
    catch (const std::exception &error)
    {
        LOG_ERROR("GateServer fatal error: ", error.what());
        shutdown();
        result = EXIT_FAILURE;
    }

    server.reset();
    logic.reset();
    LogicSystem::DestroyInstance();
    StatusGrpcClient::DestroyInstance();
    VerifyGrpcClient::DestroyInstance();
    MysqlMgr::DestroyInstance();
    RedisMgr::DestroyInstance();
    pool.reset();
    AsioIOServicePool::DestroyInstance();
    return result;
}
