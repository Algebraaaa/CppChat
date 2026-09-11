#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include "const.h"
#include "Singleton.h"
#include "ConfigMgr.h"
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "message.pb.h"
#include "data.h"
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::AddFriendReq;
using message::AddFriendRsp;

using message::AuthFriendReq;
using message::AuthFriendRsp;

using message::GetChatServerRsp;
using message::LoginRsp;
using message::LoginReq;
using message::ChatService;

using message::TextChatMsgReq;
using message::TextChatMsgRsp;
using message::TextChatData;


// ChatService Stub 池：调用其他 ChatServer 时借出一个 Stub，用完后归还。
// Stub 是 gRPC 远程代理对象，不是普通 TCP 或数据库连接。
class ChatStubPool {
public:
	ChatStubPool(std::size_t pool_size, std::string host, std::string port)
		: stopped_(false) {
		for (std::size_t index = 0; index < pool_size; ++index) {
			std::shared_ptr<Channel> channel = grpc::CreateChannel(
				host + ":" + port, grpc::InsecureChannelCredentials());
			available_stubs_.push(ChatService::NewStub(channel));
		}
	}

	~ChatStubPool() {
		std::lock_guard<std::mutex> lock(mutex_);
		Close();
		while (!available_stubs_.empty()) {
			available_stubs_.pop();
		}
	}

	// 借出一个空闲 Stub；没有空闲 Stub 时等待，连接池关闭时返回 nullptr。
	std::unique_ptr<ChatService::Stub> BorrowStub() {
		std::unique_lock<std::mutex> lock(mutex_);
		condition_.wait(lock, [this] {
			return stopped_ || !available_stubs_.empty();
			});

		if (stopped_) {
			return nullptr;
		}

		auto stub = std::move(available_stubs_.front());
		available_stubs_.pop();
		return stub;
	}

	// 把使用完的 Stub 放回空闲队列，并唤醒一个等待者。
	void ReturnStub(std::unique_ptr<ChatService::Stub> stub) {
		if (!stub) {
			return;
		}

		std::lock_guard<std::mutex> lock(mutex_);
		if (stopped_) {
			return;
		}

		available_stubs_.push(std::move(stub));
		condition_.notify_one();
	}

	// 关闭后不再借出或回收 Stub，并唤醒所有等待线程。
	void Close() {
		stopped_ = true;
		condition_.notify_all();
	}

private:
	std::atomic<bool> stopped_;
	std::queue<std::unique_ptr<ChatService::Stub>> available_stubs_;
	std::mutex mutex_;
	std::condition_variable condition_;
};

class ChatGrpcClient :public Singleton<ChatGrpcClient>
{
	friend class Singleton<ChatGrpcClient>;
public:
	~ChatGrpcClient() {

	}

	AddFriendRsp NotifyAddFriend(std::string server_name, const AddFriendReq& req);
	AuthFriendRsp NotifyAuthFriend(std::string server_name, const AuthFriendReq& req);
	bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo);
	TextChatMsgRsp NotifyTextChatMsg(std::string server_name, const TextChatMsgReq& req, const Json::Value& rtvalue);
private:
	ChatGrpcClient();
	std::unordered_map<std::string, std::unique_ptr<ChatStubPool>> chat_stub_pools_;
};