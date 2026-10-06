#include "MysqlDao.h"
#include "MysqlDaoSupport.h"
#include "const.h"
#include <algorithm>
#include <array>
#include <climits>
#include <iomanip>
#include <sstream>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
using MysqlDaoDetail::LogSqlError;
using MysqlDaoDetail::MySqlConnectionGuard;
using MysqlDaoDetail::MySqlTransaction;
using MysqlDaoDetail::ReadUserInfo;
namespace {

// PBKDF2 不会只计算一次 SHA-256，而是重复计算很多次。
// 攻击者即使拿到数据库，也需要为每次密码猜测付出更多计算成本。
constexpr int kPasswordIterations = 120000;

// 16 字节随机盐转换为十六进制后是 32 个字符。
constexpr std::size_t kPasswordSaltSize = 16;

// SHA-256 输出 32 字节，转换为十六进制后是 64 个字符。
constexpr std::size_t kPasswordHashSize = 32;

// 哈希和随机盐本来是不可直接显示的二进制字节。
// 数据库使用 CHAR 字段保存文本，因此把每个字节转换为两个十六进制字符。
std::string BytesToHex(const unsigned char *data, std::size_t size)
{
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (std::size_t index = 0; index < size; ++index) {
    output << std::setw(2) << static_cast<unsigned int>(data[index]);
  }
  return output.str();
}

/*
 * 把用户输入的明文密码转换成数据库可以安全保存的形式。
 *
 * 注意：
 * 1. password 只作为输入，不会直接写进数据库；
 * 2. 每个用户都会生成不同的随机盐，所以相同密码也会得到不同 hash；
 * 3. 登录功能以后要读取 salt、iterations，再用同样算法计算并比较 hash；
 * 4. salt_hex 和 hash_hex 是输出参数，成功时由这个函数填写。
 */
bool HashPassword(const std::string &password, std::string &salt_hex, std::string &hash_hex)
{
  // OpenSSL 接口接收 int 长度。转换前先检查，避免 size_t 转 int 溢出。
  if (password.size() > static_cast<std::size_t>(INT_MAX)) {
    return false;
  }

  // std::array 在栈上保存固定长度字节；{} 会先把全部字节初始化为 0。
  std::array<unsigned char, kPasswordSaltSize> salt{};
  std::array<unsigned char, kPasswordHashSize> hash{};

  // RAND_bytes 使用密码学安全随机数生成器创建盐，不能用 rand() 代替。
  if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
    return false;
  }

  // PBKDF2 的输入依次是：明文密码、盐、迭代次数、SHA-256、输出缓冲区。
  // 返回 1 代表成功，其他值表示 OpenSSL 计算失败。
  if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(),
                        static_cast<int>(salt.size()), kPasswordIterations, EVP_sha256(),
                        static_cast<int>(hash.size()), hash.data()) != 1) {
    return false;
  }

  // 二进制结果转成十六进制文本，对应 users 表中的 CHAR(32) 和 CHAR(64)。
  salt_hex = BytesToHex(salt.data(), salt.size());
  hash_hex = BytesToHex(hash.data(), hash.size());
  return true;
}

// 把数据库中保存的十六进制盐和哈希还原成二进制字节。
// 输入中只要出现非十六进制字符或字符数不是偶数，就说明数据库内容不合法。
bool HexToBytes(const std::string &text, std::vector<unsigned char> &bytes)
{
  if (text.empty() || text.size() % 2 != 0) {
    return false;
  }

  auto hex_value = [](char character) -> int {
    if (character >= '0' && character <= '9') {
      return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
      return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
      return character - 'A' + 10;
    }
    return -1;
  };

  bytes.clear();
  bytes.reserve(text.size() / 2);
  for (std::size_t index = 0; index < text.size(); index += 2) {
    const int high = hex_value(text[index]);
    const int low = hex_value(text[index + 1]);
    if (high < 0 || low < 0) {
      bytes.clear();
      return false;
    }
    bytes.push_back(static_cast<unsigned char>((high << 4) | low));
  }
  return true;
}

/*
 * 登录时不能再次调用 HashPassword，因为它会生成一个新的随机盐，结果必然不同。
 * 正确流程是读取注册时保存的盐和迭代次数，用相同参数重新计算候选密码的哈希，
 * 最后使用 CRYPTO_memcmp 做固定时间比较，避免把明文密码读出或打印到日志。
 */
bool VerifyPassword(const std::string &password, const std::string &salt_hex, int iterations,
                    const std::string &expected_hash_hex)
{
  if (password.empty() || password.size() > static_cast<std::size_t>(INT_MAX) || iterations <= 0) {
    return false;
  }

  std::vector<unsigned char> salt;
  std::vector<unsigned char> expected_hash;
  if (!HexToBytes(salt_hex, salt) || !HexToBytes(expected_hash_hex, expected_hash) ||
      salt.empty() || expected_hash.empty()) {
    return false;
  }

  std::vector<unsigned char> calculated_hash(expected_hash.size());
  if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(),
                        static_cast<int>(salt.size()), iterations, EVP_sha256(),
                        static_cast<int>(calculated_hash.size()), calculated_hash.data()) != 1) {
    return false;
  }

  return CRYPTO_memcmp(calculated_hash.data(), expected_hash.data(), expected_hash.size()) == 0;
}

} // namespace

int MysqlDao::RegUser(const std::string &name, const std::string &email,
                      const std::string &password)
{
  // Dao 再做一道兜底检查，避免其他调用者绕过 HTTP 层直接传入空字符串。
  if (name.empty() || email.empty() || password.empty()) {
    LOG_WARNING("MySQL registration rejected because a required field is empty.");
    return -1;
  }

  // 构造连接池失败时 _pool 为空，此时不能继续访问数据库。
  if (!_pool) {
    LOG_ERROR("MySQL registration failed because the connection pool is "
              "unavailable.");
    return -1;
  }

  // 这里先对密码进行哈希。后续 SQL 只接触 hash 和 salt，不接触明文密码字段。
  std::string salt_hex;
  std::string hash_hex;
  if (!HashPassword(password, salt_hex, hash_hex)) {
    LOG_ERROR("Password hashing failed during user registration.");
    return -1;
  }

  // 从池中借连接，并立即放进 RAII 守卫。
  // 本函数任何 return 或异常路径最终都会执行守卫析构并归还连接。
  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (connection.Get() == nullptr) {
    LOG_ERROR("MySQL registration failed because the connection pool is closed.");
    return -1;
  }

  try {
    /*
     * PreparedStatement 使用 ? 作为参数占位符。
     * 用户输入通过 setString 绑定，不会被直接拼进 SQL，能够避免 SQL 注入。
     * 参数顺序必须与 INSERT 字段顺序严格对应。
     */
    std::unique_ptr<sql::PreparedStatement> insert_statement(
        connection.Get()->prepareStatement("INSERT INTO users "
                                           "(username, email, password_hash, password_salt, "
                                           "password_iterations) "
                                           "VALUES (?, ?, ?, ?, ?)"));
    insert_statement->setString(1, name);             // 第 1 个 ?：用户名
    insert_statement->setString(2, email);            // 第 2 个 ?：邮箱
    insert_statement->setString(3, hash_hex);         // 第 3 个 ?：密码哈希
    insert_statement->setString(4, salt_hex);         // 第 4 个 ?：随机盐
    insert_statement->setInt(5, kPasswordIterations); // 第 5 个 ?：迭代次数

    // executeUpdate 用于
    // INSERT/UPDATE/DELETE，返回受影响行数；这里成功插入一行。
    insert_statement->executeUpdate();

    // LAST_INSERT_ID() 读取“当前连接刚刚插入”的自增主键。
    // 它是连接级数据，即使其他线程同时注册，也不会拿到别人的 uid。
    std::unique_ptr<sql::Statement> id_statement(connection.Get()->createStatement());
    std::unique_ptr<sql::ResultSet> id_result(
        id_statement->executeQuery("SELECT LAST_INSERT_ID() AS uid"));

    // ResultSet 初始位于第一行之前，必须先 next() 才能读取当前行。
    if (!id_result->next()) {
      LOG_ERROR("MySQL inserted a user but did not return the new uid.");
      return -1;
    }

    const int uid = id_result->getInt("uid");
    LOG_INFO("User registration stored in MySQL: uid=", uid);
    return uid;
  } catch (const sql::SQLException &exception) {
    // MySQL 错误码 1062 表示 UNIQUE 索引重复。
    // users 表对 username 和 email 都建了唯一索引，因此统一解释为用户已存在。
    if (exception.getErrorCode() == 1062) {
      LOG_WARNING("MySQL rejected registration because username or email "
                  "already exists.");
      return 0;
    }

    // 其他 SQL 异常记录错误码、SQLState 和说明，但绝不记录用户密码。
    LOG_ERROR("MySQL registration failed: code=", exception.getErrorCode(),
              ", state=", exception.getSQLState(), ", message=", exception.what());
    return -1;
  }
}

bool MysqlDao::CheckEmail(const std::string &name, const std::string &email)
{
  if (name.empty() || email.empty() || !_pool) {
    return false;
  }

  // 连接交给 RAII 守卫管理；无论从哪个分支 return，连接都会自动归还连接池。
  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (connection.Get() == nullptr) {
    LOG_ERROR("MySQL email check failed because the connection pool is unavailable.");
    return false;
  }

  try {
    // 当前数据库使用 users(username, email)，不是旧代码里的 user(name, email)。
    // 把用户名和邮箱同时写入 WHERE，可以直接判断它们是否属于同一个账号。
    std::unique_ptr<sql::PreparedStatement> check_statement(connection.Get()->prepareStatement(
        "SELECT 1 FROM users WHERE username = ? AND email = ? LIMIT 1"));
    check_statement->setString(1, name);
    check_statement->setString(2, email);

    std::unique_ptr<sql::ResultSet> result(check_statement->executeQuery());
    return result->next();
  } catch (const sql::SQLException &exception) {
    LOG_ERROR("MySQL email check failed: code=", exception.getErrorCode(),
              ", state=", exception.getSQLState(), ", message=", exception.what());
    return false;
  }
}

bool MysqlDao::CheckPwdByEmail(const std::string &email, const std::string &password,
                               UserInfo &user_info)
{
  if (email.empty() || password.empty() || !_pool) {
    return false;
  }

  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (connection.Get() == nullptr) {
    LOG_ERROR("MySQL login check failed because the connection pool is unavailable.");
    return false;
  }

  try {
    // 当前项目使用 users 表，并且只保存密码哈希、随机盐和迭代次数。
    std::unique_ptr<sql::PreparedStatement> check_statement(connection.Get()->prepareStatement(
        "SELECT id, username, email, password_hash, password_salt, "
        "password_iterations FROM users WHERE email = ? LIMIT 1"));
    check_statement->setString(1, email);

    std::unique_ptr<sql::ResultSet> result(check_statement->executeQuery());
    if (!result->next()) {
      // 不记录用户名，避免日志泄露哪些账号真实存在。
      LOG_WARNING("User login authentication failed.");
      return false;
    }

    const std::string password_hash = result->getString("password_hash");
    const std::string password_salt = result->getString("password_salt");
    const int password_iterations = result->getInt("password_iterations");
    if (!VerifyPassword(password, password_salt, password_iterations, password_hash)) {
      LOG_WARNING("User login authentication failed.");
      return false;
    }

    // 验证成功后只向上层返回业务所需信息，不保存或返回明文密码。
    user_info.name = result->getString("username");
    user_info.email = result->getString("email");
    user_info.uid = result->getInt("id");
    LOG_DEBUG("MySQL login authentication succeeded: uid=", user_info.uid);
    return true;
  } catch (const sql::SQLException &exception) {
    LOG_ERROR("MySQL login check failed: code=", exception.getErrorCode(),
              ", state=", exception.getSQLState(), ", message=", exception.what());
    return false;
  }
}

bool MysqlDao::UpdatePwd(const std::string &name, const std::string &new_password)
{
  if (name.empty() || new_password.empty() || !_pool) {
    return false;
  }

  // 密码重置和注册使用同一套 PBKDF2-HMAC-SHA256 规则，并重新生成随机盐。
  std::string salt_hex;
  std::string hash_hex;
  if (!HashPassword(new_password, salt_hex, hash_hex)) {
    LOG_ERROR("Password hashing failed during password reset.");
    return false;
  }

  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (connection.Get() == nullptr) {
    LOG_ERROR("MySQL password update failed because the connection pool is "
              "unavailable.");
    return false;
  }

  try {
    // users 表只保存 hash、salt 和迭代次数，绝不保存 new_password 明文。
    std::unique_ptr<sql::PreparedStatement> update_statement(
        connection.Get()->prepareStatement("UPDATE users SET password_hash = ?, password_salt = ?, "
                                           "password_iterations = ? WHERE username = ?"));
    update_statement->setString(1, hash_hex);
    update_statement->setString(2, salt_hex);
    update_statement->setInt(3, kPasswordIterations);
    update_statement->setString(4, name);

    const int updated_rows = update_statement->executeUpdate();
    if (updated_rows != 1) {
      LOG_WARNING("MySQL password update did not affect exactly one user row.");
      return false;
    }

    LOG_INFO("Password hash and salt were updated in MySQL.");
    return true;
  } catch (const sql::SQLException &exception) {
    LOG_ERROR("MySQL password update failed: code=", exception.getErrorCode(),
              ", state=", exception.getSQLState(), ", message=", exception.what());
    return false;
  }
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(int uid)
{
  if (uid <= 0 || !_pool) {
    return nullptr;
  }

  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (connection.Get() == nullptr) {
    return nullptr;
  }

  try {
    std::unique_ptr<sql::PreparedStatement> statement(
        connection.Get()->prepareStatement("SELECT id AS uid, username AS name, email, nick, "
                                           "profile_description AS descs, sex, icon "
                                           "FROM users WHERE id = ? LIMIT 1"));
    statement->setInt(1, uid);
    std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
    return result->next() ? ReadUserInfo(*result) : nullptr;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL user query by uid failed", exception);
    return nullptr;
  }
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(const std::string &name)
{
  if (name.empty() || !_pool) {
    return nullptr;
  }

  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (connection.Get() == nullptr) {
    return nullptr;
  }

  try {
    std::unique_ptr<sql::PreparedStatement> statement(
        connection.Get()->prepareStatement("SELECT id AS uid, username AS name, email, nick, "
                                           "profile_description AS descs, sex, icon "
                                           "FROM users WHERE username = ? LIMIT 1"));
    statement->setString(1, name);
    std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
    return result->next() ? ReadUserInfo(*result) : nullptr;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL user query by name failed", exception);
    return nullptr;
  }
}

int MysqlDao::UpdateProfile(int uid, const std::string &name, const std::string &nick,
                            const std::string &description, const std::string &avatar,
                            bool has_avatar)
{
  if (uid <= 0 || !_pool)
    return ErrorCodes::DatabaseError;
  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (!connection.Get())
    return ErrorCodes::DatabaseError;
  try {
    if (has_avatar) {
      // A separate table keeps the binary image out of login and friend-list
      // packets.
      std::unique_ptr<sql::Statement> schema(connection.Get()->createStatement());
      schema->execute("CREATE TABLE IF NOT EXISTS user_avatars ("
                      "uid INT UNSIGNED NOT NULL PRIMARY KEY, "
                      "image MEDIUMBLOB NOT NULL, "
                      "FOREIGN KEY (uid) REFERENCES users(id) ON DELETE "
                      "CASCADE) ENGINE=InnoDB");
    }
    MySqlTransaction transaction(connection.Get());
    std::unique_ptr<sql::PreparedStatement> update(connection.Get()->prepareStatement(
        "UPDATE users SET username=?, nick=?, profile_description=? WHERE "
        "id=?"));
    update->setString(1, name);
    update->setString(2, nick);
    update->setString(3, description);
    update->setInt(4, uid);
    update->executeUpdate();
    if (has_avatar) {
      std::unique_ptr<sql::PreparedStatement> image(
          connection.Get()->prepareStatement("INSERT INTO user_avatars(uid, image) VALUES(?, ?) "
                                             "ON DUPLICATE KEY UPDATE image=VALUES(image)"));
      image->setInt(1, uid);
      std::istringstream image_stream(avatar, std::ios::binary);
      image->setBlob(2, &image_stream);
      image->executeUpdate();
    }
    transaction.Commit();
    return ErrorCodes::Success;
  } catch (const sql::SQLException &exception) {
    if (exception.getErrorCode() == 1062)
      return ErrorCodes::UserExist;
    LogSqlError("MySQL profile update failed", exception);
    return ErrorCodes::DatabaseError;
  }
}

bool MysqlDao::GetAvatar(int uid, std::string &avatar)
{
  avatar.clear();
  if (uid <= 0 || !_pool)
    return false;
  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (!connection.Get())
    return false;
  try {
    std::unique_ptr<sql::PreparedStatement> query(
        connection.Get()->prepareStatement("SELECT image FROM user_avatars WHERE uid=?"));
    query->setInt(1, uid);
    std::unique_ptr<sql::ResultSet> result(query->executeQuery());
    if (result->next())
      avatar = result->getString("image");
    return true;
  } catch (const sql::SQLException &exception) {
    // Existing installations may not have uploaded an avatar yet.
    if (exception.getErrorCode() == 1146)
      return true;
    LogSqlError("MySQL avatar query failed", exception);
    return false;
  }
}
