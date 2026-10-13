#include "MysqlDao.h"
#include "ConfigMgr.h"
#include "Logger.h"
#include "MySqlPool.h"
#include <stdexcept>

namespace {
// config.ini 读取出来的值都是字符串，这个函数负责把正整数配置转成 int。
// 配置缺失、不是数字或小于等于 0 时使用 fallback，防止异常传到外层。
int ParsePositiveInt(const std::string &text, int fallback)
{
  try {
    std::size_t consumed = 0;
    const int value = std::stoi(text, &consumed);
    return consumed == text.size() && value > 0 ? value : fallback;
  } catch (...) {
    return fallback;
  }
}

} // namespace

MysqlDao::MysqlDao()
{
  // ConfigMgr 已经把 config.ini 按节读取到内存。
  // GetSection("Mysql") 得到 [Mysql] 下的全部键值。
  const SectionInfo mysql_config = ConfigMgr::GetInstance().GetSection("Mysql");

  // Host、Port 和 PoolSize 有合理默认值；账号、密码、数据库名必须明确配置。
  const std::string host = mysql_config.GetValue("Host", "127.0.0.1");
  const std::string port = mysql_config.GetValue("Port", "3306");
  const std::string user = mysql_config.GetValue("User");
  const std::string password = mysql_config.GetValue("Passwd");
  const std::string schema = mysql_config.GetValue("Schema");
  const int pool_size = ParsePositiveInt(mysql_config.GetValue("PoolSize", "5"), 5);
  MySqlPoolOptions pool_options;
  pool_options.borrow_timeout =
      std::chrono::seconds(ParsePositiveInt(mysql_config.GetValue("BorrowTimeoutSeconds", "3"), 3));
  pool_options.connect_timeout_seconds =
      ParsePositiveInt(mysql_config.GetValue("ConnectTimeoutSeconds", "3"), 3);
  pool_options.read_timeout_seconds =
      ParsePositiveInt(mysql_config.GetValue("ReadTimeoutSeconds", "5"), 5);
  pool_options.write_timeout_seconds =
      ParsePositiveInt(mysql_config.GetValue("WriteTimeoutSeconds", "5"), 5);

  // 缺少身份信息时不尝试连接，错误日志只说明缺哪个配置类别，不打印密码。
  if (user.empty() || password.empty() || schema.empty()) {
    LOG_ERROR("MySQL configuration is incomplete. Check User, Passwd and Schema.");
    throw std::runtime_error("MySQL configuration is incomplete.");
  }

  try {
    // Connector/C++ 使用 tcp://主机:端口 格式。
    // make_unique 创建连接池，并把所有权保存在 _pool 中。
    _pool = std::make_unique<MySqlPool>("tcp://" + host + ":" + port, user, password, schema,
                                        static_cast<std::size_t>(pool_size), pool_options,
                                        [](const std::string &message) { LOG_ERROR(message); });
    LOG_INFO("MysqlDao connection pool configured.");
  } catch (const std::exception &exception) {
    // 不继续向外抛：GateServer 仍可处理不依赖 MySQL 的接口。
    // 后续注册请求发现 _pool 为空时返回 DatabaseError，而不是让进程启动即崩溃。
    LOG_ERROR("MySQL DAO initialization failed: ", exception.what());
  }
}

MysqlDao::~MysqlDao()
{
  // 先显式停止连接池；unique_ptr 随后还会自动调用 MySqlPool 析构函数。
  // Close 可以重复调用，因此这里这样写是安全的。
  if (_pool) {
    _pool->Close();
  }
  LOG_INFO("MysqlDao destroyed successfully.");
}
