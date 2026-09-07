const verificationCodeKeyPrefix = 'code_';

// 名称与 GateServer 的 ErrorCodes 对应，值会通过 GetVerifyRsp.error 返回。
const ErrorCodes = {
  Success: 0,
  RedisError: 1,
  InternalError: 2,
};

module.exports = { verificationCodeKeyPrefix, ErrorCodes };
