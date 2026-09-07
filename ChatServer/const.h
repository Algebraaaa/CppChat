#pragma once

#include <cstddef>
#include <functional>
#include <utility>

enum ErrorCodes
{
	Success = 0,
	Error_Json = 1001,
	RPCFailed = 1002,
	VerifyExpired = 1003,
	VerifyCodeErr = 1004,
	UserExist = 1005,
	PasswdErr = 1006,
	EmailNotMatch = 1007,
	PasswdUpFailed = 1008,
	PasswdInvalid = 1009,
	TokenInvalid = 1010,
	UidInvalid = 1011
};

class Defer
{
public:
	explicit Defer(std::function<void()> function)
		: function_(std::move(function))
	{
	}

	~Defer()
	{
		if (function_)
		{
			function_();
		}
	}

	Defer(const Defer&) = delete;
	Defer& operator=(const Defer&) = delete;

private:
	std::function<void()> function_;
};

inline constexpr std::size_t MAX_LENGTH = 1024 * 2;
inline constexpr std::size_t HEAD_TOTAL_LEN = 4;
inline constexpr std::size_t HEAD_ID_LEN = 2;
inline constexpr std::size_t HEAD_DATA_LEN = 2;
inline constexpr std::size_t MAX_RECVQUE = 10000;
inline constexpr std::size_t MAX_SENDQUE = 1000;

enum MessageIds : short
{
	MSG_CHAT_LOGIN = 1005,
	MSG_CHAT_LOGIN_RSP = 1006
};
