const crypto = require('crypto');
const grpc = require('@grpc/grpc-js');

const messagePackage = require('./proto');
const serverConstants = require('./const');
const serverConfig = require('./config');
const emailService = require('./email');
const redisService = require('./redis');
// 2026-08-12 对比：
// 旧写法：Redis 保存600秒，但邮件提示“3分钟”，两处含义不一致。
// 新写法：统一使用 VERIFY_CODE_TTL_SECONDS = 180。
// 好处：有效期只有一个事实来源，程序行为和用户看到的文字保持一致。
const VERIFY_CODE_TTL_SECONDS = 180;

// 2026-08-12 对比：
// 旧写法：每个成功或失败分支都重复调用 gRPC 回调并手动构造响应。
// 新写法：统一交给 sendGrpcResponse() 构造响应。
// 好处：减少重复代码，新增响应字段或修改格式时只需要改一个位置。
function sendGrpcResponse(grpcCallback, emailAddress, errorCode) {
  grpcCallback(null, { email: emailAddress, error: errorCode });
}

// 函数名必须和 GateServer 共同使用的 message.proto 保持一致。
async function GetVerifyCode(grpcCall, grpcCallback) {
  const emailAddress = grpcCall.request.email;
  const verificationCodeKey =
    serverConstants.verificationCodeKeyPrefix + emailAddress;
  console.log('Verification requested.');

  try {
    let verificationCode = await redisService.getValue(verificationCodeKey);

    if (verificationCode === null) {
      // 2026-08-12 对比：
      // 旧写法：uuidv4().substring(0, 4)，会产生4位十六进制字符。
      // 新写法：crypto.randomInt() 生成100000到999999之间的6位数字。
      // 好处：随机源适合安全用途、组合更多，并且纯数字更方便用户输入。
      verificationCode = crypto.randomInt(100000, 1000000).toString();

      const verificationCodeSaved = await redisService.setValueWithExpiration(
        verificationCodeKey,
        verificationCode,
        VERIFY_CODE_TTL_SECONDS
      );

      if (!verificationCodeSaved) {
        sendGrpcResponse(
          grpcCallback,
          emailAddress,
          serverConstants.ErrorCodes.RedisError
        );
        return;
      }
    }

    const verificationEmail = {
      from: serverConfig.emailUser,
      to: emailAddress,
      subject: '验证码',
      text: `您的验证码为 ${verificationCode}，请在3分钟内完成注册。`,
    };

    await emailService.sendEmail(verificationEmail);
    sendGrpcResponse(
      grpcCallback,
      emailAddress,
      serverConstants.ErrorCodes.Success
    );
  } catch (requestError) {
    console.error('GetVerifyCode failed:', requestError.message);
    sendGrpcResponse(
      grpcCallback,
      emailAddress,
      serverConstants.ErrorCodes.InternalError
    );
  }
}

function startVerifyServer() {
  const grpcServer = new grpc.Server();
  grpcServer.addService(messagePackage.VerifyService.service, { GetVerifyCode });
  grpcServer.bindAsync(
    '0.0.0.0:50051',
    grpc.ServerCredentials.createInsecure(),
    (bindError, listeningPort) => {
      if (bindError) {
        console.error('Failed to start gRPC VerifyServer:', bindError.message);
        return;
      }

      // 2026-08-12 对比：
      // 旧写法：bindAsync 成功后继续调用 grpc-js 1.14 已弃用的 server.start()。
      // 新写法：绑定成功后直接报告实际 listeningPort，并在上方显式处理绑定错误。
      // 好处：消除弃用调用；端口被占用时不会再误报“启动成功”。
      console.log(`gRPC VerifyServer started on port ${listeningPort}`);
    }
  );
}

startVerifyServer();
