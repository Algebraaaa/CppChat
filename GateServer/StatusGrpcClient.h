#pragma once

#include <grpcpp/grpcpp.h>
#include "Singleton.h"
#include "ConfigMgr.h"
#include "Logger.h"
#include "message.grpc.pb.h"
#include "message.pb.h"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

// StatusService Stub 池：程序启动时提前创建多个 Stub，调用时借出，用完后归还。
// Stub 是 GateServer 调用 StatusServer 的远程代理对象，不是数据库或 TCP 连接。
class StatusStubPool {
public:
	StatusStubPool(std::size_t pool_size, std::string host, std::string port)
		: stopped_(false), pool_size_(pool_size), host_(host), port_(port) {
		for (std::size_t index = 0; index < pool_size_; ++index) {

			// Channel 属于 grpc 命名空间，StatusService 属于 proto 生成的 message 命名空间。
			// 这里使用完整名称，避免依赖写在类定义后面的 using 声明。
			std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(host + ":" + port,
				grpc::InsecureChannelCredentials());

			available_stubs_.push(message::StatusService::NewStub(channel));
		}
		LOG_INFO(
			"StatusService gRPC Stub pool initialized successfully: endpoint=",
			host_, ":", port_, ", stubs=", pool_size_);
	}

	~StatusStubPool() {
		std::lock_guard<std::mutex> lock(mutex_);
		Close();
		while (!available_stubs_.empty()) {
			available_stubs_.pop();
		}
		LOG_INFO("StatusService gRPC Stub pool destroyed successfully.");
	}

	// 借出一个空闲 Stub；没有空闲 Stub 时等待，连接池关闭时返回 nullptr。
	std::unique_ptr<message::StatusService::Stub> BorrowStub() {
		std::unique_lock<std::mutex> lock(mutex_);
		condition_.wait(lock, [this] {
			if (stopped_) {
				return true;
			}
			return !available_stubs_.empty();
			});
		//如果停止则直接返回空指针
		if (stopped_) {
			return  nullptr;
		}
		auto stub = std::move(available_stubs_.front());
		available_stubs_.pop();
		return stub;
	}

	// 把使用完的 Stub 放回空闲队列，并唤醒一个等待者。
	void ReturnStub(std::unique_ptr<message::StatusService::Stub> stub) {
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopped_) {
			return;
		}
		available_stubs_.push(std::move(stub));
		condition_.notify_one();
	}

	void Close() {
		stopped_ = true;
		condition_.notify_all();
	}

private:
	// stopped_ 表示池已关闭；关闭后不再借出或回收 Stub。
	std::atomic<bool> stopped_;
	std::size_t pool_size_;
	std::string host_;
	std::string port_;
	// 只保存当前没有被业务线程使用的 Stub。
	std::queue<std::unique_ptr<message::StatusService::Stub>> available_stubs_;
	std::mutex mutex_;
	std::condition_variable condition_;
};
class StatusGrpcClient :public Singleton<StatusGrpcClient>
{
	friend class Singleton<StatusGrpcClient>;
public:
	~StatusGrpcClient() {

	}
	message::GetChatServerRsp GetChatServer(int uid);

private:
	StatusGrpcClient();
	std::unique_ptr<StatusStubPool> pool_;

};
