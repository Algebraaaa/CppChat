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

// 与 StatusServer 的 ChatStubPool 使用相同命名：池中保存的是 gRPC Stub，
// 不是底层 TCP 连接。
class StatusStubPool final
{
public:
	StatusStubPool(
		std::size_t pool_size,
		const std::string& host,
		const std::string& port);
	~StatusStubPool();

	StatusStubPool(const StatusStubPool&) = delete;
	StatusStubPool& operator=(const StatusStubPool&) = delete;

	std::unique_ptr<message::StatusService::Stub> BorrowStub();
	void ReturnStub(std::unique_ptr<message::StatusService::Stub> stub);
	void Close();

private:
	std::queue<std::unique_ptr<message::StatusService::Stub>> available_stubs_;
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

	std::unique_ptr<StatusStubPool> status_stub_pool_;
};
