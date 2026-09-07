#include "VerifyGrpcClient.h"
#include <chrono>
#include <stdexcept>
#include <utility>

#include "ConfigMgr.h"
#include "const.h"
#include "Logger.h"

// 创建 gRPC Stub 对象池
RpcStubPool::RpcStubPool(std::size_t pool_size, std::string host, std::string port) {
	// 2026-08-12 对比：
	// 旧写法：pool_size 为 0 时仍创建池，BorrowStub() 随后会一直等待。
	// 新写法：构造阶段直接拒绝 0 容量。
	// 好处：尽早暴露错误，避免请求线程悄悄永久阻塞。
	if (pool_size == 0) {
		throw std::invalid_argument("gRPC Stub pool size must be greater than zero");
	}

	const std::string server_address = host + ":" + port;
	// 2026-08-12 对比：
	// 旧写法：循环5次，每个 Stub 都调用一次 CreateChannel()。
	/*for (...) {
		auto channel = grpc::CreateChannel(...);
		available_stubs_.push(
			message::VerifyService::NewStub(channel)
		);
	}*/
	// 新写法：先创建一个 Channel，再让5个 Stub 共用它。
	// 好处：Channel 才负责连接管理；复用它更符合 gRPC 模型，也减少重复对象。
	const std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(
		// InsecureChannelCredentials()表示这个Channel不使用TLS加密和服务器证书验证
		server_address, grpc::InsecureChannelCredentials());

	for (std::size_t index = 0; index < pool_size; ++index) {
		available_stubs_.push(message::VerifyService::NewStub(channel));
	}
	LOG_INFO(
		"VerifyService gRPC Stub pool initialized successfully: endpoint=",
		server_address, ", stubs=", pool_size);
}

RpcStubPool::~RpcStubPool() {
	// 析构清理队列期间持有互斥锁，避免其他线程同时修改 available_stubs_。
	std::lock_guard<std::mutex> lock(mutex_);
	// 设置停止状态，并唤醒可能正在 BorrowStub() 中等待的线程。
	Close();
	// 清空队列；每次 pop 都会销毁其中的 unique_ptr，进而销毁 Stub。
	while (!available_stubs_.empty()) {
		available_stubs_.pop();
	}
	LOG_INFO("VerifyService gRPC Stub pool destroyed successfully.");
}

void RpcStubPool::Close() {
	// 原子标记改成 true，表示连接池不再提供连接。
	stopped_ = true;
	// 唤醒所有等待者，让它们检查 stopped_ 并及时退出。
	condition_.notify_all();
}

std::unique_ptr<message::VerifyService::Stub> RpcStubPool::BorrowStub() {
	// unique_lock 会锁住 mutex_；与 lock_guard 不同，它能配合条件变量临时解锁。
	std::unique_lock<std::mutex> lock(mutex_);
	// 队列为空时进入等待。等待期间 condition_ 会暂时释放 mutex_，
	// 因此其他线程仍能进入 ReturnStub() 归还 Stub。
	condition_.wait(lock, [this]() {
		// 满足任一条件就结束等待：连接池关闭，或者队列中已有空闲 Stub。
		return stopped_ || !available_stubs_.empty();
		});

	// 如果是因为连接池关闭而醒来，就用 nullptr 表示无法再借出 Stub。
	if (stopped_) {
		return nullptr;
	}

	// unique_ptr 不能复制，只能 move；这里把队首 Stub 的所有权转给调用者。
	auto stub = std::move(available_stubs_.front());
	// 队首 unique_ptr 已经被移走，把空的队列元素删除。
	available_stubs_.pop();
	// 返回借出的 Stub。函数结束时 unique_lock 会自动解锁 mutex_。
	return stub;
}

void RpcStubPool::ReturnStub(
	std::unique_ptr<message::VerifyService::Stub> stub) {
	// 2026-08-12 对比：
	// 旧写法：不检查参数，nullptr 也可能被放回队列。
	// 新写法：发现空 Stub 就立即返回。
	// 好处：保证池中的每个元素都可调用，避免下一次借出后空指针崩溃。
	if (!stub) {
		return;
	}

	// 归还时也必须加锁，因为 available_stubs_ 可能被多个线程同时访问。
	std::lock_guard<std::mutex> lock(mutex_);
	// 连接池已经关闭时不再放回队列；参数离开作用域后会自动销毁 Stub。
	if (stopped_) {
		return;
	}

	// 把调用者持有的 Stub 所有权移回空闲队列。
	available_stubs_.push(std::move(stub));
	// 唤醒一个等待 Stub 的线程。只归还一个，所以 notify_one 就足够。
	condition_.notify_one();
}

// 第一次调用 VerifyGrpcClient::GetInstance() 时，会执行这个私有构造函数。
VerifyGrpcClient::VerifyGrpcClient() {
	// 取得全局唯一的配置管理器。
	auto& gCfgMgr = ConfigMgr::GetInstance();
	// 2026-08-12 对比：
	// 旧写法：gCfgMgr["VerifyServer"]["Host"]，两层 [] 要靠读者推断含义。
	// 新写法：先 GetSection("VerifyServer")，再 GetValue("Host")。
	// 好处：代码直接表达“取分组、取配置值”两步，断点调试也更方便。
	const SectionInfo verify_server = gCfgMgr.GetSection("VerifyServer");
	const std::string host = verify_server.GetValue("Host");
	const std::string port = verify_server.GetValue("Port");

	// 2026-08-09 修改：启动 gRPC 客户端前检查 VerifyServer 配置是否完整。
	if (host.empty() || port.empty()) {
		throw std::runtime_error("VerifyServer Host or Port is missing in config.ini");
	}

	// 创建容量为 5 的 Stub 池，并把它交给 pool_ 独占管理。
	pool_ = std::make_unique<RpcStubPool>(5, host, port);
}

// 完成一次“根据邮箱获取验证码”的同步 gRPC 调用。
message::GetVerifyRsp VerifyGrpcClient::GetVerifyCode(const std::string& email) {
	// ClientContext 只服务于本次 RPC，保存超时、元数据、取消状态等调用信息。
	grpc::ClientContext context;
	// reply 用来接收 VerifyServer 返回的 GetVerifyRsp。
	message::GetVerifyRsp reply;
	// request 是即将发送给 VerifyServer 的 GetVerifyReq。
	message::GetVerifyReq request;
	// 把函数收到的邮箱写入 Protobuf 请求对象的 email 字段。
	request.set_email(email);

	// 2026-08-09 修改：同步 RPC 最多等待 20 秒，避免工作线程被长期阻塞。
	context.set_deadline(
		std::chrono::system_clock::now() + std::chrono::seconds(20));
	// TODO
	// 更正式的架构则是把邮件发送交给异步任务队列，接口只返回“验证码任务已受理”，避免客户端一直等待 SMTP
	// 但以当前项目规模，先把 gRPC 超时调整到20秒最合适

	// 从连接池借出一个 VerifyService::Stub。
	auto stub = pool_->BorrowStub();
	// 连接池关闭时 BorrowStub() 可能返回空指针。
	if (!stub) {
		reply.set_error(ErrorCodes::RPCFailed);
		return reply;
	}

	// 通过 Stub 同步调用远端 VerifyServer 的 GetVerifyCode。
	// 这行会等待服务器响应、超时或发生网络错误以后才继续往下执行。
	// status 描述 RPC 传输是否成功；reply 保存服务器返回的业务数据。
	grpc::Status status = stub->GetVerifyCode(&context, request, &reply);
	// 无论这次 RPC 成功还是失败，都把 Stub 归还给连接池供下次复用。
	pool_->ReturnStub(std::move(stub));
	// status.ok() == false 表示网络、服务器不可达、超时等 RPC 层错误。
	if (!status.ok()) {
		LOG_ERROR(
			"GetVerifyCode RPC failed: ", status.error_message());
		// 把 RPC 层错误转换为本项目统一使用的 RPCFailed 错误码。
		reply.set_error(ErrorCodes::RPCFailed);
	}
	// 返回响应。RPC 成功时里面是 VerifyServer 的业务结果；失败时带 RPCFailed
	return reply;
}
