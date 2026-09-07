#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

#include <grpcpp/grpcpp.h>

#include "message.grpc.pb.h"
#include "Singleton.h"
// Stub池，程序启动时提前创建多个 Stub；每次发送验证码请求时借出一个，用完后归还
// Stub 可以理解为 VerifyServer 在 GateServer 进程里的“远程代理对象”
class RpcStubPool {
public:
	// pool_size：池中准备多少个 Stub
	// host、port：VerifyServer 的 IP 地址和端口，例如 127.0.0.1:50051
	RpcStubPool(std::size_t pool_size, std::string host, std::string port);
	~RpcStubPool();

	// 关闭连接池，通知所有正在等待 Stub 的线程停止等待
	void Close();

	std::unique_ptr<message::VerifyService::Stub> BorrowStub();

	void ReturnStub(std::unique_ptr<message::VerifyService::Stub> stub);

private:
	// 原子停止标记。atomic 保证多个线程读写该标记时不会产生数据竞争
	std::atomic<bool> stopped_{ false };
	// 空闲 Stub 队列。队首借出，使用完毕后放回队尾
	std::queue<std::unique_ptr<message::VerifyService::Stub>> available_stubs_;
	// 保护 available_stubs_ 队列，防止多个工作线程同时修改队列
	std::mutex mutex_;
	// 当队列为空时负责等待；有 Stub 被归还或连接池关闭时负责唤醒线程
	std::condition_variable condition_;
};

// VerifyGrpcClient 是 GateServer 访问 VerifyServer 的统一入口。
// 它继承 Singleton，所以整个 GateServer 进程只创建一个 VerifyGrpcClient
class VerifyGrpcClient : public Singleton<VerifyGrpcClient>
{
	// Singleton<VerifyGrpcClient> 需要调用下面的私有构造函数，
	// 所以把 Singleton 模板声明为友元。
	friend class Singleton<VerifyGrpcClient>;
public:
	// 根据邮箱向 VerifyServer 请求验证码。
	// 参数 email 是用户邮箱；返回值包含业务错误码等响应数据
	message::GetVerifyRsp GetVerifyCode(const std::string& email);

private:
	// VerifyGrpcClient 独占一个 Stub 连接池，由 unique_ptr 自动管理其生命周期
	std::unique_ptr<RpcStubPool> pool_;
	// 构造函数私有，禁止外部随意创建对象，只能通过 GetInstance() 使用单例
	VerifyGrpcClient();
};
