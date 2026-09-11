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
	UidInvalid = 1011,
	CreateChatFailed = 1012,
	LoadChatFailed = 1013
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
	MSG_CHAT_LOGIN_RSP = 1006,
	ID_SEARCH_USER_REQ = 1007,
	ID_SEARCH_USER_RSP = 1008,
	ID_ADD_FRIEND_REQ = 1009,
	ID_ADD_FRIEND_RSP = 1010,
	ID_NOTIFY_ADD_FRIEND_REQ = 1011,
	ID_AUTH_FRIEND_REQ = 1013,
	ID_AUTH_FRIEND_RSP = 1014,
	ID_NOTIFY_AUTH_FRIEND_REQ = 1015,
	ID_TEXT_CHAT_MSG_REQ = 1017,
	ID_TEXT_CHAT_MSG_RSP = 1018,
	ID_NOTIFY_TEXT_CHAT_MSG_REQ = 1019,
	ID_NOTIFY_OFF_LINE_REQ = 1021,
	ID_HEART_BEAT_REQ = 1023,
	ID_HEARTBEAT_RSP = 1024,
	ID_LOAD_CHAT_THREAD_REQ = 1025,
	ID_LOAD_CHAT_THREAD_RSP = 1026,
	ID_CREATE_PRIVATE_CHAT_REQ = 1027,
	ID_CREATE_PRIVATE_CHAT_RSP = 1028,
	ID_LOAD_CHAT_MSG_REQ = 1029,
	ID_LOAD_CHAT_MSG_RSP = 1030
};

inline constexpr char USER_IP_PREFIX[] = "uip_";
inline constexpr char USER_TOKEN_PREFIX[] = "utoken_";
inline constexpr char USER_BASE_INFO[] = "ubaseinfo_";
inline constexpr char LOGIN_COUNT[] = "logincount";
inline constexpr char NAME_INFO[] = "nameinfo_";
inline constexpr char LOCK_PREFIX[] = "lock_";
inline constexpr char USER_SESSION_PREFIX[] = "usession_";

inline constexpr int LOCK_TIME_OUT = 10;
inline constexpr int ACQUIRE_TIME_OUT = 5;
inline constexpr int HEARTBEAT_TIMEOUT_SECONDS = 20;
inline constexpr int HEARTBEAT_SCAN_SECONDS = 10;
