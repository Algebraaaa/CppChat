#pragma once

// StatusServer 通过 gRPC 回包返回业务结果；数值与调用方约定保持一致。
enum ErrorCodes {
	Success = 0,      // 成功找到一个可用的 ChatServer
	RPCFailed = 1002, // StatusServer 无法完成本次分配
	TokenInvalid = 1010,
	UidInvalid = 1011
};
