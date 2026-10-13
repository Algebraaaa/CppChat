#pragma once

#include <memory>
#include <string>

class MySqlPool;

// 数据访问结果；由 HTTP 层映射为协议错误码。
enum class AccountCheckResult
{
    Match,
    NoMatch,
    DatabaseError
};
enum class PasswordUpdateResult
{
    Updated,
    Failed,
    DatabaseError
};

struct UserInfo
{
    std::string name;
    int uid = 0;
    std::string email;
};

class MysqlDao
{
  public:
    MysqlDao();
    ~MysqlDao();

    // name/email/password 来自通过基础校验后的注册请求。
    // 返回值约定：
    //   > 0：注册成功，返回数据库自动生成的用户 uid；
    //   = 0：用户名或邮箱触发唯一索引，说明用户已经存在；
    //   = -1：连接、密码哈希或 SQL 执行出现错误。
    int RegUser(const std::string &name, const std::string &email, const std::string &password);
    // 区分业务不匹配、更新失败与数据库故障。
    AccountCheckResult CheckEmail(const std::string &name, const std::string &email);
    PasswordUpdateResult UpdatePwd(const std::string &name, const std::string &new_password);
    AccountCheckResult CheckPwdByEmail(const std::string &email, const std::string &password,
                                       UserInfo &user_info);

  private:
    // MysqlDao 独占连接池；Dao 析构时连接池也会自动析构。
    std::unique_ptr<MySqlPool> _pool;
};
