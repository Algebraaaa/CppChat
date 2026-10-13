#include "MysqlMgr.h"

MysqlMgr::~MysqlMgr() {}

int MysqlMgr::RegUser(const std::string &name, const std::string &email,
                      const std::string &password)
{
    return _dao.RegUser(name, email, password);
}

MysqlMgr::MysqlMgr() {}
AccountCheckResult MysqlMgr::CheckEmail(const std::string &name, const std::string &email)
{
    return _dao.CheckEmail(name, email);
}

PasswordUpdateResult MysqlMgr::UpdatePwd(const std::string &name, const std::string &new_password)
{
    return _dao.UpdatePwd(name, new_password);
}
AccountCheckResult MysqlMgr::CheckPwdByEmail(const std::string &email, const std::string &password,
                                             UserInfo &user_info)
{
    return _dao.CheckPwdByEmail(email, password, user_info);
}
