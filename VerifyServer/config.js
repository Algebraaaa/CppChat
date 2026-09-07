const fs = require('fs');

const configFileText = fs.readFileSync('config.json', 'utf8');
const configFile = JSON.parse(configFileText);

const emailUser = configFile.email.user;
const emailAuthorizationCode = configFile.email.pass;
const mysqlHost = configFile.mysql.host;
const mysqlPort = configFile.mysql.port;
const redisHost = configFile.redis.host;
const redisPort = configFile.redis.port;
const redisPassword = configFile.redis.password;

module.exports = {
  emailUser,
  emailAuthorizationCode,
  mysqlHost,
  mysqlPort,
  redisHost,
  redisPort,
  redisPassword,
};
