const verificationCodeKeyPrefix = 'code_';

// 名称与 GateServer 的 ErrorCodes 对应，值会通过 GetVerifyRsp.error 返回。
const ErrorCodes = {
  NetworkError: -1,
  Success: 0,
  Error_Json: 1001,
  RPCFailed: 1002,
  VerifyExpired: 1003,
  VerifyCodeErr: 1004,
  UserExist: 1005,
  PasswdErr: 1006,
  EmailNotMatch: 1007,
  PasswdUpFailed: 1008,
  PasswdInvalid: 1009,
  TokenInvalid: 1010,
  UidInvalid: 1011,
  CreateChatFailed: 1012,
  LoadChatFailed: 1013,
  DatabaseError: 1014,
  RedisError: 1015,
  InternalError: 1016,
};

module.exports = { verificationCodeKeyPrefix, ErrorCodes };
