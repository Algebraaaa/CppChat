#pragma once

// StatusServer 通过 gRPC 回包返回业务结果；数值与调用方约定保持一致。
enum ErrorCodes {
	NetworkError = -1, // 客户端本地网络错误，服务端不返回
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
	LoadChatFailed = 1013,
	DatabaseError = 1014,
	RedisError = 1015,
	InternalError = 1016,
};
