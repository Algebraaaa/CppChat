#pragma once
enum ErrorCodes {
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
	DatabaseError = 1010,
};
// Defer类
class Defer {
public:
	// 接受一个lambda表达式或者函数指针
	Defer(std::function<void()> func) : func_(func) {}

	// 析构函数中执行传入的函数
	~Defer() {
		func_();
	}

private:
	std::function<void()> func_;
};
