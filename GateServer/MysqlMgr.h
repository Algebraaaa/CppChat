#pragma once
#include "MysqlDao.h"
#include "Singleton.h"
class MysqlMgr : public Singleton<MysqlMgr>
{
    friend class Singleton<MysqlMgr>;

  public:
    ~MysqlMgr();
    int RegUser(const std::string &name, const std::string &email, const std::string &password);
    AccountCheckResult CheckEmail(const std::string &name, const std::string &email);
    PasswordUpdateResult UpdatePwd(const std::string &name, const std::string &new_password);
    AccountCheckResult CheckPwdByEmail(const std::string &email, const std::string &password,
                                       UserInfo &user_info);

  private:
    MysqlMgr();
    MysqlDao _dao;
};
