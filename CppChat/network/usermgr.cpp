#include "network/usermgr.h"

UserMgr::~UserMgr() {}

void UserMgr::SetName(QString name)
{
  _name = name;
}

void UserMgr::SetUid(int uid)
{
  _uid = uid;
}

void UserMgr::SetToken(QString token)
{
  _token = token;
}

const QString UserMgr::GetName() const
{
  return _name;
}

std::vector<std::shared_ptr<ApplyInfo>> UserMgr::GetApplyList()
{
  return _apply_list;
}

UserMgr::UserMgr() {}
