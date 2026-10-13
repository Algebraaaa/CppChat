const fs = require('fs');

const configFileText = fs.readFileSync(process.env.CPPCHAT_VERIFY_CONFIG || 'config.json', 'utf8');
const configFile = JSON.parse(configFileText);

const emailUser = configFile.email.user;
const emailAuthorizationCode = configFile.email.pass;
const mysqlHost = configFile.mysql.host;
const mysqlPort = configFile.mysql.port;
const redisHost = configFile.redis.host;
const redisPort = configFile.redis.port;
const redisPassword = configFile.redis.password;

module.exports = {
  listenAddress: configFile.listen || '0.0.0.0:50051',
  smtpHost: configFile.email.host || 'smtp.163.com',
  smtpPort: configFile.email.port || 465,
  smtpSecure: configFile.email.secure === undefined ? true : configFile.email.secure,
  smtpAuth: configFile.email.auth === undefined ? true : configFile.email.auth,
  emailUser,
  emailAuthorizationCode,
  mysqlHost,
  mysqlPort,
  redisHost,
  redisPort,
  redisPassword,
};
