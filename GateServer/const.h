#pragma once
#include <exception>
#include <functional>
#include <utility>

enum ErrorCodes {
	NetworkError = -1, // 客户端本地网络错误，服务端不返回
	Success = 0,
	Error_Json = 1001,  //Json解析错误
	RPCFailed = 1002,  //RPC请求错误
	VerifyExpired = 1003,
	VerifyCodeErr = 1004,
	UserExist = 1005,
	PasswdErr = 1006,
	EmailNotMatch = 1007,
	PasswdUpFailed = 1008,
	PasswdInvalid = 1009,
	TokenInvalid = 1010,
	UidInvalid = 1011,
	CreateChatFailed = 1012,
	LoadChatFailed = 1013,
	DatabaseError = 1014,
	RedisError = 1015,
	InternalError = 1016,
};
// Defer：作用域退出时自动执行一次清理操作。
// 2026-09-12 对比：
// 旧写法允许复制，多个副本析构时会重复执行同一清理操作；也不能取消清理。
// 新写法禁止复制、支持安全的移动构造，并提供 Cancel() 取消尚未执行的操作。
// 好处：适合管理连接池 Stub 等“借出后必须归还”的资源，避免提前 return 或异常造成遗漏。
class Defer {
public:
	// explicit 防止 std::function 被意外地隐式转换为 Defer；移动保存可减少一次复制。
	explicit Defer(std::function<void()> func)
		: func_(std::move(func)) {}

	// 一个清理操作只能由一个 Defer 对象负责，禁止复制可避免执行两次。
	Defer(const Defer&) = delete;
	Defer& operator=(const Defer&) = delete;

	// 允许把清理责任转交给新对象；被移动对象立即失效，析构时不会重复执行。
	Defer(Defer&& other) noexcept
		: func_(std::move(other.func_)), active_(other.active_) {
		other.active_ = false;
	}

	// 作用域清理器不需要移动赋值；删除它可以避免覆盖仍处于活动状态的清理操作。
	Defer& operator=(Defer&&) = delete;

	// 析构函数不能把异常传播到外部。清理函数本身应保证不抛异常；
	// 如果违反约定，std::terminate() 会明确终止程序，避免异常析构期间出现不一致的资源状态。
	~Defer() noexcept {
		if (!active_) {
			return;
		}

		active_ = false;
		try {
			if (func_) {
				func_();
			}
		}
		catch (...) {
			std::terminate();
		}
	}

	// 已经手动完成清理时可调用 Cancel()，防止析构时再次执行。
	void Cancel() noexcept {
		active_ = false;
	}

private:
	std::function<void()> func_;
	bool active_ = true;
};
