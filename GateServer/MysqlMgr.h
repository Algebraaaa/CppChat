#pragma once
#include "MysqlDao.h"
#include "Singleton.h"
class MysqlMgr : public Singleton<MysqlMgr>
{
    friend class Singleton<MysqlMgr>;
public:
    ~MysqlMgr();
    int RegUser(const std::string& name, const std::string& email, const std::string& password);
    bool CheckEmail(const std::string& name, const std::string& email);
    bool UpdatePwd(const std::string& name, const std::string& new_password);
    bool CheckPwdByEmail(const std::string& email, const std::string& password, UserInfo& user_info);
private:
    MysqlMgr();
    MysqlDao  _dao;
};
