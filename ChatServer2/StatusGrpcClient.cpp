#include "StatusGrpcClient.h"

#include <chrono>
#include <stdexcept>
#include <utility>

#include "ConfigMgr.h"
#include "Logger.h"
#include "const.h"

StatusStubPool::StatusStubPool(
	std::size_t pool_size,
	const std::string& host,
	const std::string& port)
{
	if (pool_size == 0)
	{
		throw std::invalid_argument("Status gRPC pool size must be positive");
	}

	for (std::size_t index = 0; index < pool_size; ++index)
	{
		auto channel = grpc::CreateChannel(
			host + ':' + port,
			grpc::InsecureChannelCredentials());
		available_stubs_.push(message::StatusService::NewStub(channel));
	}
}

StatusStubPool::~StatusStubPool()
{
	Close();
}

std::unique_ptr<message::StatusService::Stub>
StatusStubPool::BorrowStub()
{
	std::unique_lock<std::mutex> lock(mutex_);
	condition_.wait(lock, [this]()
	{
		return stopped_ || !available_stubs_.empty();
	});

	if (stopped_)
	{
		return nullptr;
	}

	auto stub = std::move(available_stubs_.front());
	available_stubs_.pop();
	return stub;
}

void StatusStubPool::ReturnStub(
	std::unique_ptr<message::StatusService::Stub> stub)
{
	if (!stub)
	{
		return;
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopped_)
		{
			return;
		}
		available_stubs_.push(std::move(stub));
	}
	condition_.notify_one();
}

void StatusStubPool::Close()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopped_)
		{
			return;
		}
		stopped_ = true;
		while (!available_stubs_.empty())
		{
			available_stubs_.pop();
		}
	}
	condition_.notify_all();
}

StatusGrpcClient::StatusGrpcClient()
{
	const auto& config = ConfigMgr::GetInstance();
	const SectionInfo status_server = config["StatusServer"];
	const std::string host = status_server["Host"];
	const std::string port = status_server["Port"];
	const std::string pool_size_text = status_server.GetValue("PoolSize", "5");

	if (host.empty() || port.empty())
	{
		throw std::runtime_error(
			"StatusServer.Host or StatusServer.Port is missing in config.ini");
	}

	const std::size_t pool_size = std::stoul(pool_size_text);
	status_stub_pool_ = std::make_unique<StatusStubPool>(pool_size, host, port);
	LOG_INFO("StatusGrpcClient initialized: endpoint=", host, ':', port,
		", pool size=", pool_size, '.');
}

message::LoginRsp StatusGrpcClient::Login(
	int uid,
	const std::string& token)
{
	message::LoginRsp reply;
	message::LoginReq request;
	request.set_uid(uid);
	request.set_token(token);

	auto stub = status_stub_pool_->BorrowStub();
	if (!stub)
	{
		LOG_ERROR("StatusServer Login failed: no gRPC Stub is available.");
		reply.set_error(ErrorCodes::RPCFailed);
		return reply;
	}

	Defer return_stub_after_call([this, &stub]()
	{
		status_stub_pool_->ReturnStub(std::move(stub));
	});

	grpc::ClientContext context;
	context.set_deadline(
		std::chrono::system_clock::now() + std::chrono::seconds(5));

	const grpc::Status status = stub->Login(&context, request, &reply);
	if (status.ok())
	{
		LOG_DEBUG("StatusServer Login completed: uid=", uid,
			", business error=", reply.error(), '.');
		return reply;
	}

	LOG_ERROR("StatusServer Login RPC failed: uid=", uid,
		", grpc code=", static_cast<int>(status.error_code()),
		", message=", status.error_message());
	reply.set_error(ErrorCodes::RPCFailed);
	return reply;
}
