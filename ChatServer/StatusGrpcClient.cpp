#include "StatusGrpcClient.h"

#include <chrono>
#include <stdexcept>
#include <utility>

#include "ConfigMgr.h"
#include "Logger.h"
#include "const.h"

StatusConPool::StatusConPool(
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
		connections_.push(message::StatusService::NewStub(channel));
	}
}

StatusConPool::~StatusConPool()
{
	Close();
}

std::unique_ptr<message::StatusService::Stub>
StatusConPool::GetConnection()
{
	std::unique_lock<std::mutex> lock(mutex_);
	condition_.wait(lock, [this]()
	{
		return stopped_ || !connections_.empty();
	});

	if (stopped_)
	{
		return nullptr;
	}

	auto connection = std::move(connections_.front());
	connections_.pop();
	return connection;
}

void StatusConPool::ReturnConnection(
	std::unique_ptr<message::StatusService::Stub> connection)
{
	if (!connection)
	{
		return;
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopped_)
		{
			return;
		}
		connections_.push(std::move(connection));
	}
	condition_.notify_one();
}

void StatusConPool::Close()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopped_)
		{
			return;
		}
		stopped_ = true;
		while (!connections_.empty())
		{
			connections_.pop();
		}
	}
	condition_.notify_all();
}

StatusGrpcClient::StatusGrpcClient()
{
	const auto& config = ConfigMgr::Inst();
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
	pool_ = std::make_unique<StatusConPool>(pool_size, host, port);
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

	auto stub = pool_->GetConnection();
	if (!stub)
	{
		LOG_ERROR("StatusServer Login failed: no gRPC connection is available.");
		reply.set_error(ErrorCodes::RPCFailed);
		return reply;
	}

	Defer return_connection([this, &stub]()
	{
		pool_->ReturnConnection(std::move(stub));
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
