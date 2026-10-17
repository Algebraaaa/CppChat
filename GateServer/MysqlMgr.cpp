#include "MysqlMgr.h"

MysqlMgr::~MysqlMgr() {}

int MysqlMgr::RegUser(const std::string &name, const std::string &email,
                      const std::string &password)
{
    return _dao.RegUser(name, email, password);
}

MysqlMgr::MysqlMgr() {}
AccountCheckResult MysqlMgr::CheckEmail(const std::string &name, const std::string &email,
                                        int &verified_uid)
{
    return _dao.CheckEmail(name, email, verified_uid);
}

PasswordUpdateResult MysqlMgr::UpdatePwd(int verified_uid, const std::string &verified_email,
                                         const std::string &new_password)
{
    return _dao.UpdatePwd(verified_uid, verified_email, new_password);
}
AccountCheckResult MysqlMgr::CheckPwdByEmail(const std::string &email, const std::string &password,
                                             UserInfo &user_info)
{
    return _dao.CheckPwdByEmail(email, password, user_info);
}
