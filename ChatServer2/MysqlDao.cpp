#include "MysqlDao.h"

#include <algorithm>
#include <array>
#include <climits>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <mysql_driver.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include "ConfigMgr.h"
#include "Logger.h"
#include "const.h"

namespace
{
	// PBKDF2 不会只计算一次 SHA-256，而是重复计算很多次。
	// 攻击者即使拿到数据库，也需要为每次密码猜测付出更多计算成本。
	constexpr int kPasswordIterations = 120000;

	// 16 字节随机盐转换为十六进制后是 32 个字符。
	constexpr std::size_t kPasswordSaltSize = 16;

	// SHA-256 输出 32 字节，转换为十六进制后是 64 个字符。
	constexpr std::size_t kPasswordHashSize = 32;

	// config.ini 读取出来的值都是字符串，这个函数负责把正整数配置转成 int。
	// 配置缺失、不是数字或小于等于 0 时使用 fallback，防止异常传到外层。
	int ParsePositiveInt(const std::string& text, int fallback)
	{
		try
		{
			const int value = std::stoi(text);
			return value > 0 ? value : fallback;
		}
		catch (...)
		{
			return fallback;
		}
	}

	// 哈希和随机盐本来是不可直接显示的二进制字节。
	// 数据库使用 CHAR 字段保存文本，因此把每个字节转换为两个十六进制字符。
	std::string BytesToHex(const unsigned char* data, std::size_t size)
	{
		std::ostringstream output;
		output << std::hex << std::setfill('0');
		for (std::size_t index = 0; index < size; ++index)
		{
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
	bool HashPassword(const std::string& password, std::string& salt_hex, std::string& hash_hex)
	{
		// OpenSSL 接口接收 int 长度。转换前先检查，避免 size_t 转 int 溢出。
		if (password.size() > static_cast<std::size_t>(INT_MAX))
		{
			return false;
		}

		// std::array 在栈上保存固定长度字节；{} 会先把全部字节初始化为 0。
		std::array<unsigned char, kPasswordSaltSize> salt{};
		std::array<unsigned char, kPasswordHashSize> hash{};

		// RAND_bytes 使用密码学安全随机数生成器创建盐，不能用 rand() 代替。
		if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1)
		{
			return false;
		}

		// PBKDF2 的输入依次是：明文密码、盐、迭代次数、SHA-256、输出缓冲区。
		// 返回 1 代表成功，其他值表示 OpenSSL 计算失败。
		if (PKCS5_PBKDF2_HMAC(
			password.data(),
			static_cast<int>(password.size()),
			salt.data(),
			static_cast<int>(salt.size()),
			kPasswordIterations,
			EVP_sha256(),
			static_cast<int>(hash.size()),
			hash.data()) != 1)
		{
			return false;
		}

		// 二进制结果转成十六进制文本，对应 users 表中的 CHAR(32) 和 CHAR(64)。
		salt_hex = BytesToHex(salt.data(), salt.size());
		hash_hex = BytesToHex(hash.data(), hash.size());
		return true;
	}

	// 把数据库中保存的十六进制盐和哈希还原成二进制字节。
	// 输入中只要出现非十六进制字符或字符数不是偶数，就说明数据库内容不合法。
	bool HexToBytes(const std::string& text, std::vector<unsigned char>& bytes)
	{
		if (text.empty() || text.size() % 2 != 0)
		{
			return false;
		}

		auto hex_value = [](char character) -> int {
			if (character >= '0' && character <= '9')
			{
				return character - '0';
			}
			if (character >= 'a' && character <= 'f')
			{
				return character - 'a' + 10;
			}
			if (character >= 'A' && character <= 'F')
			{
				return character - 'A' + 10;
			}
			return -1;
		};

		bytes.clear();
		bytes.reserve(text.size() / 2);
		for (std::size_t index = 0; index < text.size(); index += 2)
		{
			const int high = hex_value(text[index]);
			const int low = hex_value(text[index + 1]);
			if (high < 0 || low < 0)
			{
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
	bool VerifyPassword(
		const std::string& password,
		const std::string& salt_hex,
		int iterations,
		const std::string& expected_hash_hex)
	{
		if (password.empty() || password.size() > static_cast<std::size_t>(INT_MAX) ||
			iterations <= 0)
		{
			return false;
		}

		std::vector<unsigned char> salt;
		std::vector<unsigned char> expected_hash;
		if (!HexToBytes(salt_hex, salt) || !HexToBytes(expected_hash_hex, expected_hash) ||
			salt.empty() || expected_hash.empty())
		{
			return false;
		}

		std::vector<unsigned char> calculated_hash(expected_hash.size());
		if (PKCS5_PBKDF2_HMAC(
			password.data(),
			static_cast<int>(password.size()),
			salt.data(),
			static_cast<int>(salt.size()),
			iterations,
			EVP_sha256(),
			static_cast<int>(calculated_hash.size()),
			calculated_hash.data()) != 1)
		{
			return false;
		}

		return CRYPTO_memcmp(
			calculated_hash.data(),
			expected_hash.data(),
			expected_hash.size()) == 0;
	}

	/*
	 * MySqlConnectionGuard 是“作用域连接守卫”，体现 RAII 思想：
	 * 构造时接管从连接池借来的 connection；离开当前作用域时析构，自动归还连接。
	 *
	 * 因此 RegUser 中无论正常 return，还是 SQL 抛出异常，都不需要手写多次
	 * ReturnConnection，也不会因为遗漏某个 return 分支而永久丢失连接。
	 */
	class MySqlConnectionGuard
	{
	public:
		MySqlConnectionGuard(MySqlPool* pool, std::unique_ptr<sql::Connection> connection) : _pool(pool), _connection(std::move(connection)) {}

		~MySqlConnectionGuard()
		{
			// 只有连接池和连接都有效时才归还；std::move 转移 unique_ptr 所有权。
			if (_pool != nullptr && _connection != nullptr)
			{
				_pool->ReturnConnection(std::move(_connection));
			}
		}

		// 对外返回普通指针只是为了调用 Connection 方法，所有权仍属于守卫。
		sql::Connection* Get() const
		{
			return _connection.get();
		}

	private:
		MySqlPool* _pool;
		std::unique_ptr<sql::Connection> _connection;
	};

	class MySqlTransaction
	{
	public:
		explicit MySqlTransaction(sql::Connection* connection)
			: _connection(connection)
		{
			_connection->setAutoCommit(false);
		}

		~MySqlTransaction()
		{
			if (_connection == nullptr)
			{
				return;
			}

			try
			{
				if (!_finished)
				{
					_connection->rollback();
				}
				_connection->setAutoCommit(true);
			}
			catch (...)
			{
			}
		}

		void Commit()
		{
			_connection->commit();
			_finished = true;
			_connection->setAutoCommit(true);
		}

	private:
		sql::Connection* _connection;
		bool _finished = false;
	};

	void LogSqlError(const char* operation, const sql::SQLException& exception)
	{
		LOG_ERROR(
			operation,
			": code=", exception.getErrorCode(),
			", state=", exception.getSQLState(),
			", message=", exception.what());
	}

	std::shared_ptr<UserInfo> ReadUserInfo(sql::ResultSet& result)
	{
		auto user = std::make_shared<UserInfo>();
		user->uid = result.getInt("uid");
		user->name = result.getString("name");
		user->email = result.getString("email");
		user->nick = result.getString("nick");
		user->desc = result.getString("descs");
		user->sex = result.getInt("sex");
		user->icon = result.getString("icon");
		return user;
	}
}

MySqlPool::MySqlPool(
	const std::string& url,
	const std::string& user,
	const std::string& password,
	const std::string& schema,
	std::size_t pool_size)
	// 初始化列表先保存配置，再进入构造函数体创建连接。
	// 这些值来自构造函数参数，而不是要求外部传入成员变量本身。
	: _url(url), _user(user), _password(password), _schema(schema)
{
	// 大小为 0 的连接池永远借不到连接，因此直接拒绝这种配置。
	if (pool_size == 0)
	{
		throw std::invalid_argument("MySQL pool size must be greater than zero.");
	}

	// Connector/C++ 提供一个驱动实例，它负责根据地址和账号创建 Connection。
	// driver 由库管理，本类只使用它，不负责 delete。
	sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();

	try
	{
		// 启动时一次性创建 pool_size 个连接，并全部放入“空闲连接队列”。
		for (std::size_t index = 0; index < pool_size; ++index)
		{
			// driver->connect 返回堆上的 Connection，立即交给 unique_ptr 管理。
			std::unique_ptr<sql::Connection> connection(driver->connect(_url, _user, _password));

			// 选择默认数据库，以后 SQL 可以直接写 users，不必写 cppchat.users。
			connection->setSchema(_schema);

			// move 后，局部变量不再拥有连接，队列成为新的唯一所有者。
			_connections.push(std::move(connection));
		}
	}
	catch (const sql::SQLException& exception)
	{
		LOG_ERROR(
			"MySQL connection pool initialization failed: code=",
			exception.getErrorCode(), ", state=", exception.getSQLState(),
			", message=", exception.what());
		// 构造阶段如果有连接创建失败，重新抛出异常，不留下“看似可用”的空连接池。
		throw;
	}

	LOG_INFO(
		"MySQL connection pool initialized successfully: schema=", _schema,
		", connections=", pool_size);
}

MySqlPool::~MySqlPool()
{
	// 先唤醒并停止可能仍在等待连接的线程。
	Close();

	// 清空 unique_ptr 队列时，每个 Connection 都会自动析构并断开连接。
	std::lock_guard<std::mutex> lock(_mutex);
	while (!_connections.empty())
	{
		_connections.pop();
	}
	LOG_INFO("MySQL connection pool destroyed successfully.");
}

std::unique_ptr<sql::Connection> MySqlPool::GetConnection()
{
	// unique_lock 与 lock_guard 不同：condition_variable::wait 等待期间需要临时解锁，
	// 让其他线程能够进入 ReturnConnection 把连接放回队列。
	std::unique_lock<std::mutex> lock(_mutex);

	// wait 的条件为：连接池已停止，或者至少有一个空闲连接。
	// 条件不满足时线程休眠，不会一直循环浪费 CPU。
	_condition.wait(lock, [this]() {
		return _stopped || !_connections.empty();
		});

	// Close 会将 _stopped 设为 true 并唤醒等待者，此时不能再借出连接。
	if (_stopped)
	{
		return nullptr;
	}

	// 取出队首连接并从队列删除。返回 unique_ptr 后，调用者暂时拥有它。
	auto connection = std::move(_connections.front());
	_connections.pop();
	return connection;
}

void MySqlPool::ReturnConnection(
	std::unique_ptr<sql::Connection> connection)
{
	// 空指针不是有效连接，直接忽略，防止把 nullptr 放进队列。
	if (connection == nullptr)
	{
		return;
	}

	{
		// 修改共享队列和 _stopped 前加锁，防止多个线程同时操作造成数据竞争。
		std::lock_guard<std::mutex> lock(_mutex);
		if (_stopped)
		{
			// 连接池已经关闭时不再入队；函数结束会销毁 unique_ptr 和连接。
			return;
		}
		_connections.push(std::move(connection));
	}

	// 队列已经有连接，唤醒一个等待线程。通知放在解锁后可减少无谓竞争。
	_condition.notify_one();
}

void MySqlPool::Close()
{
	{
		// _stopped 和连接队列使用同一把锁，保证等待条件检查的一致性。
		std::lock_guard<std::mutex> lock(_mutex);
		_stopped = true;
	}

	// 必须唤醒全部等待者，否则它们可能永远睡眠，导致程序退出时卡住。
	_condition.notify_all();
}

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

	// 缺少身份信息时不尝试连接，错误日志只说明缺哪个配置类别，不打印密码。
	if (user.empty() || password.empty() || schema.empty())
	{
		LOG_ERROR("MySQL configuration is incomplete. Check User, Passwd and Schema.");
		throw std::runtime_error("MySQL configuration is incomplete.");
	}

	try
	{
		// Connector/C++ 使用 tcp://主机:端口 格式。
		// make_unique 创建连接池，并把所有权保存在 _pool 中。
		_pool = std::make_unique<MySqlPool>(
			"tcp://" + host + ":" + port,
			user,
			password,
			schema,
			static_cast<std::size_t>(pool_size));
		LOG_INFO("MysqlDao initialized successfully.");
	}
	catch (const std::exception& exception)
	{
		// 不继续向外抛：GateServer 仍可处理不依赖 MySQL 的接口。
		// 后续注册请求发现 _pool 为空时返回 DatabaseError，而不是让进程启动即崩溃。
		LOG_ERROR("MySQL DAO initialization failed: ", exception.what());
	}
}

MysqlDao::~MysqlDao()
{
	// 先显式停止连接池；unique_ptr 随后还会自动调用 MySqlPool 析构函数。
	// Close 可以重复调用，因此这里这样写是安全的。
	if (_pool)
	{
		_pool->Close();
	}
	LOG_INFO("MysqlDao destroyed successfully.");
}

int MysqlDao::RegUser(
	const std::string& name,
	const std::string& email,
	const std::string& password)
{
	// Dao 再做一道兜底检查，避免其他调用者绕过 HTTP 层直接传入空字符串。
	if (name.empty() || email.empty() || password.empty())
	{
		LOG_WARNING("MySQL registration rejected because a required field is empty.");
		return -1;
	}

	// 构造连接池失败时 _pool 为空，此时不能继续访问数据库。
	if (!_pool)
	{
		LOG_ERROR("MySQL registration failed because the connection pool is unavailable.");
		return -1;
	}

	// 这里先对密码进行哈希。后续 SQL 只接触 hash 和 salt，不接触明文密码字段。
	std::string salt_hex;
	std::string hash_hex;
	if (!HashPassword(password, salt_hex, hash_hex))
	{
		LOG_ERROR("Password hashing failed during user registration.");
		return -1;
	}

	// 从池中借连接，并立即放进 RAII 守卫。
	// 本函数任何 return 或异常路径最终都会执行守卫析构并归还连接。
	MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		LOG_ERROR("MySQL registration failed because the connection pool is closed.");
		return -1;
	}

	try
	{
		/*
		 * PreparedStatement 使用 ? 作为参数占位符。
		 * 用户输入通过 setString 绑定，不会被直接拼进 SQL，能够避免 SQL 注入。
		 * 参数顺序必须与 INSERT 字段顺序严格对应。
		 */
		std::unique_ptr<sql::PreparedStatement> insert_statement(
			connection.Get()->prepareStatement(
				"INSERT INTO users "
				"(username, email, password_hash, password_salt, password_iterations) "
				"VALUES (?, ?, ?, ?, ?)"));
		insert_statement->setString(1, name);               // 第 1 个 ?：用户名
		insert_statement->setString(2, email);              // 第 2 个 ?：邮箱
		insert_statement->setString(3, hash_hex);            // 第 3 个 ?：密码哈希
		insert_statement->setString(4, salt_hex);            // 第 4 个 ?：随机盐
		insert_statement->setInt(5, kPasswordIterations);    // 第 5 个 ?：迭代次数

		// executeUpdate 用于 INSERT/UPDATE/DELETE，返回受影响行数；这里成功插入一行。
		insert_statement->executeUpdate();

		// LAST_INSERT_ID() 读取“当前连接刚刚插入”的自增主键。
		// 它是连接级数据，即使其他线程同时注册，也不会拿到别人的 uid。
		std::unique_ptr<sql::Statement> id_statement(
			connection.Get()->createStatement());
		std::unique_ptr<sql::ResultSet> id_result(
			id_statement->executeQuery("SELECT LAST_INSERT_ID() AS uid"));

		// ResultSet 初始位于第一行之前，必须先 next() 才能读取当前行。
		if (!id_result->next())
		{
			LOG_ERROR("MySQL inserted a user but did not return the new uid.");
			return -1;
		}

		const int uid = id_result->getInt("uid");
		LOG_INFO("User registration stored in MySQL: uid=", uid);
		return uid;
	}
	catch (const sql::SQLException& exception)
	{
		// MySQL 错误码 1062 表示 UNIQUE 索引重复。
		// users 表对 username 和 email 都建了唯一索引，因此统一解释为用户已存在。
		if (exception.getErrorCode() == 1062)
		{
			LOG_WARNING("MySQL rejected registration because username or email already exists.");
			return 0;
		}

		// 其他 SQL 异常记录错误码、SQLState 和说明，但绝不记录用户密码。
		LOG_ERROR(
			"MySQL registration failed: code=", exception.getErrorCode(),
			", state=", exception.getSQLState(),
			", message=", exception.what());
		return -1;
	}
}
bool MysqlDao::CheckEmail(const std::string& name, const std::string& email)
{
	if (name.empty() || email.empty() || !_pool)
	{
		return false;
	}

	// 连接交给 RAII 守卫管理；无论从哪个分支 return，连接都会自动归还连接池。
	MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		LOG_ERROR("MySQL email check failed because the connection pool is unavailable.");
		return false;
	}

	try
	{
		// 当前数据库使用 users(username, email)，不是旧代码里的 user(name, email)。
		// 把用户名和邮箱同时写入 WHERE，可以直接判断它们是否属于同一个账号。
		std::unique_ptr<sql::PreparedStatement> check_statement(
			connection.Get()->prepareStatement(
				"SELECT 1 FROM users WHERE username = ? AND email = ? LIMIT 1"));
		check_statement->setString(1, name);
		check_statement->setString(2, email);

		std::unique_ptr<sql::ResultSet> result(check_statement->executeQuery());
		return result->next();
	}
	catch (const sql::SQLException& exception)
	{
		LOG_ERROR(
			"MySQL email check failed: code=", exception.getErrorCode(),
			", state=", exception.getSQLState(),
			", message=", exception.what());
		return false;
	}
}
bool MysqlDao::CheckPwdByEmail(
	const std::string& email,
	const std::string& password,
	UserInfo& user_info)
{
	if (email.empty() || password.empty() || !_pool)
	{
		return false;
	}

	MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		LOG_ERROR("MySQL login check failed because the connection pool is unavailable.");
		return false;
	}

	try
	{
		// 当前项目使用 users 表，并且只保存密码哈希、随机盐和迭代次数。
		std::unique_ptr<sql::PreparedStatement> check_statement(
			connection.Get()->prepareStatement(
				"SELECT id, username, email, password_hash, password_salt, "
				"password_iterations FROM users WHERE email = ? LIMIT 1"));
		check_statement->setString(1, email);

		std::unique_ptr<sql::ResultSet> result(check_statement->executeQuery());
		if (!result->next())
		{
			// 不记录用户名，避免日志泄露哪些账号真实存在。
			LOG_WARNING("User login authentication failed.");
			return false;
		}

		const std::string password_hash = result->getString("password_hash");
		const std::string password_salt = result->getString("password_salt");
		const int password_iterations = result->getInt("password_iterations");
		if (!VerifyPassword(password, password_salt, password_iterations, password_hash))
		{
			LOG_WARNING("User login authentication failed.");
			return false;
		}

		// 验证成功后只向上层返回业务所需信息，不保存或返回明文密码。
		user_info.name = result->getString("username");
		user_info.email = result->getString("email");
		user_info.uid = result->getInt("id");
		LOG_DEBUG("MySQL login authentication succeeded: uid=", user_info.uid);
		return true;
	}
	catch (const sql::SQLException& exception)
	{
		LOG_ERROR(
			"MySQL login check failed: code=", exception.getErrorCode(),
			", state=", exception.getSQLState(),
			", message=", exception.what());
		return false;
	}
}
bool MysqlDao::UpdatePwd(const std::string& name, const std::string& new_password)
{
	if (name.empty() || new_password.empty() || !_pool)
	{
		return false;
	}

	// 密码重置和注册使用同一套 PBKDF2-HMAC-SHA256 规则，并重新生成随机盐。
	std::string salt_hex;
	std::string hash_hex;
	if (!HashPassword(new_password, salt_hex, hash_hex))
	{
		LOG_ERROR("Password hashing failed during password reset.");
		return false;
	}

	MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		LOG_ERROR("MySQL password update failed because the connection pool is unavailable.");
		return false;
	}

	try
	{
		// users 表只保存 hash、salt 和迭代次数，绝不保存 new_password 明文。
		std::unique_ptr<sql::PreparedStatement> update_statement(
			connection.Get()->prepareStatement(
				"UPDATE users SET password_hash = ?, password_salt = ?, "
				"password_iterations = ? WHERE username = ?"));
		update_statement->setString(1, hash_hex);
		update_statement->setString(2, salt_hex);
		update_statement->setInt(3, kPasswordIterations);
		update_statement->setString(4, name);

		const int updated_rows = update_statement->executeUpdate();
		if (updated_rows != 1)
		{
			LOG_WARNING("MySQL password update did not affect exactly one user row.");
			return false;
		}

		LOG_INFO("Password hash and salt were updated in MySQL.");
		return true;
	}
	catch (const sql::SQLException& exception)
	{
		LOG_ERROR(
			"MySQL password update failed: code=", exception.getErrorCode(),
			", state=", exception.getSQLState(),
			", message=", exception.what());
		return false;
	}
}

bool MysqlDao::AddFriendApply(
	int from_uid,
	int to_uid,
	const std::string& description,
	const std::string& remark)
{
	if (from_uid <= 0 || to_uid <= 0 || from_uid == to_uid || !_pool)
	{
		return false;
	}

	MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	try
	{
		std::unique_ptr<sql::PreparedStatement> statement(
			connection.Get()->prepareStatement(
				"INSERT INTO friend_apply (from_uid, to_uid, descs, back_name) "
				"VALUES (?, ?, ?, ?) "
				"ON DUPLICATE KEY UPDATE descs = VALUES(descs), "
				"back_name = VALUES(back_name), status = 0"));
		statement->setInt(1, from_uid);
		statement->setInt(2, to_uid);
		statement->setString(3, description);
		statement->setString(4, remark);
		return statement->executeUpdate() >= 0;
	}
	catch (const sql::SQLException& exception)
	{
		LogSqlError("MySQL friend application write failed", exception);
		return false;
	}
}

bool MysqlDao::AuthFriendApply(int from_uid, int to_uid)
{
	if (from_uid <= 0 || to_uid <= 0 || !_pool)
	{
		return false;
	}

	MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	try
	{
		std::unique_ptr<sql::PreparedStatement> statement(
			connection.Get()->prepareStatement(
				"UPDATE friend_apply SET status = 1 "
				"WHERE from_uid = ? AND to_uid = ?"));
		statement->setInt(1, to_uid);
		statement->setInt(2, from_uid);
		return statement->executeUpdate() == 1;
	}
	catch (const sql::SQLException& exception)
	{
		LogSqlError("MySQL friend application authorization failed", exception);
		return false;
	}
}

bool MysqlDao::AddFriend(
	int from_uid,
	int to_uid,
	const std::string& remark,
	std::vector<std::shared_ptr<ChatMessage>>& initial_messages)
{
	if (from_uid <= 0 || to_uid <= 0 || from_uid == to_uid || !_pool)
	{
		return false;
	}

	MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	try
	{
		MySqlTransaction transaction(connection.Get());
		std::string reverse_remark;
		std::string application_description;

		{
			std::unique_ptr<sql::PreparedStatement> statement(
				connection.Get()->prepareStatement(
					"SELECT back_name, descs FROM friend_apply "
					"WHERE from_uid = ? AND to_uid = ? FOR UPDATE"));
			statement->setInt(1, to_uid);
			statement->setInt(2, from_uid);
			std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
			if (!result->next())
			{
				return false;
			}
			reverse_remark = result->getString("back_name");
			application_description = result->getString("descs");
		}

		{
			std::unique_ptr<sql::PreparedStatement> statement(
				connection.Get()->prepareStatement(
					"UPDATE friend_apply SET status = 1 "
					"WHERE from_uid = ? AND to_uid = ?"));
			statement->setInt(1, to_uid);
			statement->setInt(2, from_uid);
			if (statement->executeUpdate() != 1)
			{
				return false;
			}
		}

		auto add_friend_row = [&](int self_uid, int friend_uid, const std::string& back_name)
		{
			std::unique_ptr<sql::PreparedStatement> statement(
				connection.Get()->prepareStatement(
					"INSERT INTO friend (self_id, friend_id, back) VALUES (?, ?, ?) "
					"ON DUPLICATE KEY UPDATE back = VALUES(back)"));
			statement->setInt(1, self_uid);
			statement->setInt(2, friend_uid);
			statement->setString(3, back_name);
			statement->executeUpdate();
		};
		add_friend_row(from_uid, to_uid, remark);
		add_friend_row(to_uid, from_uid, reverse_remark);

		const int first_uid = std::min(from_uid, to_uid);
		const int second_uid = std::max(from_uid, to_uid);
		int thread_id = 0;
		{
			std::unique_ptr<sql::PreparedStatement> statement(
				connection.Get()->prepareStatement(
					"SELECT thread_id FROM private_chat "
					"WHERE user1_id = ? AND user2_id = ? FOR UPDATE"));
			statement->setInt(1, first_uid);
			statement->setInt(2, second_uid);
			std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
			if (result->next())
			{
				thread_id = result->getInt("thread_id");
			}
		}

		if (thread_id == 0)
		{
			std::unique_ptr<sql::PreparedStatement> thread_statement(
				connection.Get()->prepareStatement(
					"INSERT INTO chat_thread (type, created_at) VALUES ('private', NOW())"));
			thread_statement->executeUpdate();
			std::unique_ptr<sql::Statement> id_statement(connection.Get()->createStatement());
			std::unique_ptr<sql::ResultSet> id_result(
				id_statement->executeQuery("SELECT LAST_INSERT_ID() AS thread_id"));
			if (!id_result->next())
			{
				return false;
			}
			thread_id = id_result->getInt("thread_id");

			std::unique_ptr<sql::PreparedStatement> private_statement(
				connection.Get()->prepareStatement(
					"INSERT INTO private_chat "
					"(thread_id, user1_id, user2_id, created_at) "
					"VALUES (?, ?, ?, NOW())"));
			private_statement->setInt64(1, thread_id);
			private_statement->setInt(2, first_uid);
			private_statement->setInt(3, second_uid);
			private_statement->executeUpdate();
		}

		auto add_initial_message = [&](int sender_uid, int receiver_uid, const std::string& content)
		{
			if (content.empty())
			{
				return;
			}
			std::unique_ptr<sql::PreparedStatement> statement(
				connection.Get()->prepareStatement(
					"INSERT INTO chat_message "
					"(thread_id, sender_id, recv_id, content, created_at, updated_at, status) "
					"VALUES (?, ?, ?, ?, NOW(), NOW(), 2)"));
			statement->setInt64(1, thread_id);
			statement->setInt(2, sender_uid);
			statement->setInt(3, receiver_uid);
			statement->setString(4, content);
			statement->executeUpdate();

			std::unique_ptr<sql::Statement> id_statement(connection.Get()->createStatement());
			std::unique_ptr<sql::ResultSet> id_result(
				id_statement->executeQuery("SELECT LAST_INSERT_ID() AS message_id"));
			if (id_result->next())
			{
				auto message = std::make_shared<ChatMessage>();
				message->message_id = id_result->getInt("message_id");
				message->thread_id = thread_id;
				message->sender_id = sender_uid;
				message->recv_id = receiver_uid;
				message->content = content;
				message->status = 2;
				initial_messages.push_back(std::move(message));
			}
		};

		add_initial_message(to_uid, from_uid, application_description);
		add_initial_message(from_uid, to_uid, "We are friends now!");
		transaction.Commit();
		return true;
	}
	catch (const sql::SQLException& exception)
	{
		LogSqlError("MySQL friend creation failed", exception);
		return false;
	}
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(int uid)
{
	if (uid <= 0 || !_pool)
	{
		return nullptr;
	}

	MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return nullptr;
	}

	try
	{
		std::unique_ptr<sql::PreparedStatement> statement(
			connection.Get()->prepareStatement(
				"SELECT id AS uid, username AS name, email, nick, "
				"profile_description AS descs, sex, icon "
				"FROM users WHERE id = ? LIMIT 1"));
		statement->setInt(1, uid);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		return result->next() ? ReadUserInfo(*result) : nullptr;
	}
	catch (const sql::SQLException& exception)
	{
		LogSqlError("MySQL user query by uid failed", exception);
		return nullptr;
	}
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(const std::string& name)
{
	if (name.empty() || !_pool)
	{
		return nullptr;
	}

	MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return nullptr;
	}

	try
	{
		std::unique_ptr<sql::PreparedStatement> statement(
			connection.Get()->prepareStatement(
				"SELECT id AS uid, username AS name, email, nick, "
				"profile_description AS descs, sex, icon "
				"FROM users WHERE username = ? LIMIT 1"));
		statement->setString(1, name);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		return result->next() ? ReadUserInfo(*result) : nullptr;
	}
	catch (const sql::SQLException& exception)
	{
		LogSqlError("MySQL user query by name failed", exception);
		return nullptr;
	}
}

bool MysqlDao::GetApplyList(
	int to_uid,
	std::vector<std::shared_ptr<ApplyInfo>>& applications,
	int begin_id,
	int limit)
{
	if (to_uid <= 0 || limit <= 0 || !_pool)
	{
		return false;
	}

	MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	try
	{
		std::unique_ptr<sql::PreparedStatement> statement(
			connection.Get()->prepareStatement(
				"SELECT a.from_uid, a.descs, a.status, u.username AS name, "
				"u.nick, u.sex, u.icon "
				"FROM friend_apply AS a JOIN users AS u ON a.from_uid = u.id "
				"WHERE a.to_uid = ? AND a.id > ? ORDER BY a.id ASC LIMIT ?"));
		statement->setInt(1, to_uid);
		statement->setInt(2, begin_id);
		statement->setInt(3, limit);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		while (result->next())
		{
			applications.push_back(std::make_shared<ApplyInfo>(
				result->getInt("from_uid"),
				result->getString("name"),
				result->getString("descs"),
				result->getString("icon"),
				result->getString("nick"),
				result->getInt("sex"),
				result->getInt("status")));
		}
		return true;
	}
	catch (const sql::SQLException& exception)
	{
		LogSqlError("MySQL friend application query failed", exception);
		return false;
	}
}

bool MysqlDao::GetFriendList(
	int self_uid,
	std::vector<std::shared_ptr<UserInfo>>& friends)
{
	if (self_uid <= 0 || !_pool)
	{
		return false;
	}

	MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	try
	{
		std::unique_ptr<sql::PreparedStatement> statement(
			connection.Get()->prepareStatement(
				"SELECT u.id AS uid, u.username AS name, u.email, u.nick, "
				"u.profile_description AS descs, "
				"u.sex, u.icon, f.back FROM friend AS f "
				"JOIN users AS u ON f.friend_id = u.id WHERE f.self_id = ?"));
		statement->setInt(1, self_uid);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		while (result->next())
		{
			auto user = ReadUserInfo(*result);
			user->back = result->getString("back");
			friends.push_back(std::move(user));
		}
		return true;
	}
	catch (const sql::SQLException& exception)
	{
		LogSqlError("MySQL friend list query failed", exception);
		return false;
	}
}

bool MysqlDao::GetUserThreads(
	std::int64_t user_id,
	std::int64_t last_id,
	int page_size,
	std::vector<std::shared_ptr<ChatThreadInfo>>& threads,
	bool& load_more,
	int& next_last_id)
{
	load_more = false;
	next_last_id = last_id;
	threads.clear();
	if (user_id <= 0 || page_size <= 0 || !_pool)
	{
		return false;
	}

	MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	try
	{
		std::unique_ptr<sql::PreparedStatement> statement(
			connection.Get()->prepareStatement(
				"WITH all_threads AS ("
				" SELECT thread_id, 'private' AS type, user1_id, user2_id"
				" FROM private_chat WHERE (user1_id = ? OR user2_id = ?) AND thread_id > ?"
				" UNION ALL"
				" SELECT thread_id, 'group' AS type, 0 AS user1_id, 0 AS user2_id"
				" FROM group_chat_member WHERE user_id = ? AND thread_id > ?"
				") SELECT thread_id, type, user1_id, user2_id FROM all_threads"
				" ORDER BY thread_id LIMIT ?"));
		statement->setInt64(1, user_id);
		statement->setInt64(2, user_id);
		statement->setInt64(3, last_id);
		statement->setInt64(4, user_id);
		statement->setInt64(5, last_id);
		statement->setInt(6, page_size + 1);

		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		while (result->next())
		{
			auto thread = std::make_shared<ChatThreadInfo>();
			thread->_thread_id = result->getInt("thread_id");
			thread->_type = result->getString("type");
			thread->_user1_id = result->getInt("user1_id");
			thread->_user2_id = result->getInt("user2_id");
			threads.push_back(std::move(thread));
		}

		if (static_cast<int>(threads.size()) > page_size)
		{
			load_more = true;
			threads.pop_back();
		}
		if (!threads.empty())
		{
			next_last_id = threads.back()->_thread_id;
		}
		return true;
	}
	catch (const sql::SQLException& exception)
	{
		LogSqlError("MySQL chat thread query failed", exception);
		return false;
	}
}

bool MysqlDao::CreatePrivateChat(
	int user1_uid,
	int user2_uid,
	int& thread_id)
{
	thread_id = 0;
	if (user1_uid <= 0 || user2_uid <= 0 || user1_uid == user2_uid || !_pool)
	{
		return false;
	}

	MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	try
	{
		MySqlTransaction transaction(connection.Get());
		const int first_uid = std::min(user1_uid, user2_uid);
		const int second_uid = std::max(user1_uid, user2_uid);
		std::unique_ptr<sql::PreparedStatement> query(
			connection.Get()->prepareStatement(
				"SELECT thread_id FROM private_chat "
				"WHERE user1_id = ? AND user2_id = ? FOR UPDATE"));
		query->setInt(1, first_uid);
		query->setInt(2, second_uid);
		std::unique_ptr<sql::ResultSet> result(query->executeQuery());
		if (result->next())
		{
			thread_id = result->getInt("thread_id");
			transaction.Commit();
			return true;
		}

		std::unique_ptr<sql::PreparedStatement> create_thread(
			connection.Get()->prepareStatement(
				"INSERT INTO chat_thread (type, created_at) VALUES ('private', NOW())"));
		create_thread->executeUpdate();
		std::unique_ptr<sql::Statement> id_statement(connection.Get()->createStatement());
		std::unique_ptr<sql::ResultSet> id_result(
			id_statement->executeQuery("SELECT LAST_INSERT_ID() AS thread_id"));
		if (!id_result->next())
		{
			return false;
		}
		thread_id = id_result->getInt("thread_id");

		std::unique_ptr<sql::PreparedStatement> create_private_chat(
			connection.Get()->prepareStatement(
				"INSERT INTO private_chat "
				"(thread_id, user1_id, user2_id, created_at) VALUES (?, ?, ?, NOW())"));
		create_private_chat->setInt64(1, thread_id);
		create_private_chat->setInt(2, first_uid);
		create_private_chat->setInt(3, second_uid);
		create_private_chat->executeUpdate();
		transaction.Commit();
		return true;
	}
	catch (const sql::SQLException& exception)
	{
		LogSqlError("MySQL private chat creation failed", exception);
		return false;
	}
}

std::shared_ptr<PageResult> MysqlDao::LoadChatMsg(
	int thread_id,
	int last_message_id,
	int page_size)
{
	if (thread_id <= 0 || page_size <= 0 || !_pool)
	{
		return nullptr;
	}

	MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return nullptr;
	}

	try
	{
		auto page = std::make_shared<PageResult>();
		page->next_cursor = last_message_id;
		std::unique_ptr<sql::PreparedStatement> statement(
			connection.Get()->prepareStatement(
				"SELECT message_id, thread_id, sender_id, recv_id, unique_id, content, "
				"created_at, status FROM chat_message "
				"WHERE thread_id = ? AND message_id > ? "
				"ORDER BY message_id ASC LIMIT ?"));
		statement->setInt64(1, thread_id);
		statement->setInt64(2, last_message_id);
		statement->setInt(3, page_size + 1);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		while (result->next())
		{
			ChatMessage message;
			message.message_id = result->getInt("message_id");
			message.thread_id = result->getInt("thread_id");
			message.sender_id = result->getInt("sender_id");
			message.recv_id = result->getInt("recv_id");
			message.unique_id = result->getString("unique_id");
			message.content = result->getString("content");
			message.chat_time = result->getString("created_at");
			message.status = result->getInt("status");
			page->messages.push_back(std::move(message));
		}

		if (static_cast<int>(page->messages.size()) > page_size)
		{
			page->load_more = true;
			page->messages.pop_back();
		}
		if (!page->messages.empty())
		{
			page->next_cursor = page->messages.back().message_id;
		}
		return page;
	}
	catch (const sql::SQLException& exception)
	{
		LogSqlError("MySQL chat message query failed", exception);
		return nullptr;
	}
}

bool MysqlDao::AddChatMsg(std::vector<std::shared_ptr<ChatMessage>>& messages)
{
	if (messages.empty() || !_pool)
	{
		return false;
	}

	MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	try
	{
		MySqlTransaction transaction(connection.Get());
		std::unique_ptr<sql::PreparedStatement> statement(
			connection.Get()->prepareStatement(
				"INSERT INTO chat_message "
				"(thread_id, sender_id, recv_id, unique_id, content, created_at, updated_at, status) "
				"VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
		for (const auto& message : messages)
		{
			if (!message)
			{
				continue;
			}
			statement->setInt64(1, message->thread_id);
			statement->setInt(2, message->sender_id);
			statement->setInt(3, message->recv_id);
			statement->setString(4, message->unique_id);
			statement->setString(5, message->content);
			statement->setString(6, message->chat_time);
			statement->setString(7, message->chat_time);
			statement->setInt(8, message->status);
			statement->executeUpdate();

			std::unique_ptr<sql::Statement> id_statement(connection.Get()->createStatement());
			std::unique_ptr<sql::ResultSet> id_result(
				id_statement->executeQuery("SELECT LAST_INSERT_ID() AS message_id"));
			if (id_result->next())
			{
				message->message_id = id_result->getInt("message_id");
			}
		}
		transaction.Commit();
		return true;
	}
	catch (const sql::SQLException& exception)
	{
		LogSqlError("MySQL chat message write failed", exception);
		return false;
	}
}
