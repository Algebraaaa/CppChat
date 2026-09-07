#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

#include <grpcpp/grpcpp.h>

#include "Singleton.h"
#include "message.grpc.pb.h"
#include "message.pb.h"

// 复用 GateServer 的 Stub 连接池结构，供 ChatServer 调用 StatusServer。
class StatusConPool final
{
public:
	StatusConPool(
		std::size_t pool_size,
		const std::string& host,
		const std::string& port);
	~StatusConPool();

	StatusConPool(const StatusConPool&) = delete;
	StatusConPool& operator=(const StatusConPool&) = delete;

	std::unique_ptr<message::StatusService::Stub> GetConnection();
	void ReturnConnection(
		std::unique_ptr<message::StatusService::Stub> connection);
	void Close();

private:
	std::queue<std::unique_ptr<message::StatusService::Stub>> connections_;
	std::mutex mutex_;
	std::condition_variable condition_;
	bool stopped_ = false;
};

class StatusGrpcClient final : public Singleton<StatusGrpcClient>
{
	friend class Singleton<StatusGrpcClient>;

public:
	~StatusGrpcClient() = default;

	message::LoginRsp Login(int uid, const std::string& token);

private:
	StatusGrpcClient();

	std::unique_ptr<StatusConPool> pool_;
};
