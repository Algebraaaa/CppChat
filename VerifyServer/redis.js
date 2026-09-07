const IORedis = require('ioredis');
const serverConfig = require('./config');

// VerifyServer 只创建一个 Redis 客户端，ioredis 会管理连接和自动重连。
const redisClient = new IORedis({
  host: serverConfig.redisHost,
  port: serverConfig.redisPort,
  password: serverConfig.redisPassword,
});
redisClient.on('error', (connectionError) => {
  // 2026-08-12 对比：
  // 旧写法：Redis 出现一次 error 就调用 quit()，主动结束客户端。
  // 新写法：记录具体错误，但让 ioredis 自己执行自动重连。
  // 好处：Redis 短暂重启或网络抖动后服务可以自动恢复，不必重启 VerifyServer。
  console.error('Redis connection error:', connectionError.message);
});

async function getValue(redisKey) {
  try {
    return await redisClient.get(redisKey);
  } catch (getError) {
    console.error('Redis GET failed:', getError.message);
    return null;
  }
}

async function keyExists(redisKey) {
  try {
    return await redisClient.exists(redisKey);
  } catch (queryError) {
    console.error('Redis EXISTS failed:', queryError.message);
    return 0;
  }
}

async function setValueWithExpiration(redisKey, storedValue, expireSeconds) {
  try {
    // 2026-08-12 对比：
    // 旧写法：先 SET，再单独 EXPIRE，需要两条命令。
    // 新写法：SET key value EX seconds，一条命令同时写值和过期时间。
    // 好处：操作具有原子性，不会因第二条命令失败而留下永不过期的验证码。
    await redisClient.set(redisKey, storedValue, 'EX', expireSeconds);
    return true;
  } catch (setError) {
    console.error('Redis SET EX failed:', setError.message);
    return false;
  }
}

function closeConnection() {
  return redisClient.quit();
}

module.exports = {
  getValue,
  keyExists,
  setValueWithExpiration,
  closeConnection,
};
