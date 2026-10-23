#include "UserMgr.h"
#include "CSession.h"

UserMgr::~UserMgr() { _uid_to_session.clear(); }

std::shared_ptr<CSession> UserMgr::GetSession(int uid, const std::string &device)
{
  std::lock_guard<std::mutex> lock(_session_mtx);
  auto iter = _uid_to_session.find(uid);
  if (iter == _uid_to_session.end()) {
    return nullptr;
  }

  auto slot = iter->second.find(device);
  return slot == iter->second.end() ? nullptr : slot->second;
}

std::vector<std::shared_ptr<CSession>> UserMgr::GetSessions(int uid)
{
  std::lock_guard<std::mutex> lock(_session_mtx);
  std::vector<std::shared_ptr<CSession>> result;
  const auto found = _uid_to_session.find(uid);
  if (found != _uid_to_session.end())
    for (const auto &slot : found->second)
      if (slot.second && !slot.second->IsClosed())
        result.push_back(slot.second);
  return result;
}

void UserMgr::SetUserSession(int uid, std::shared_ptr<CSession> session)
{
  std::lock_guard<std::mutex> lock(_session_mtx);
  _uid_to_session[uid][session->GetDevice()] = session;
}

UserMgr::UserMgr() {}

void UserMgr::RmvUserSession(int uid, const std::string &session_id)
{
  std::lock_guard<std::mutex> lock(_session_mtx);
  const auto session = _uid_to_session.find(uid);
  if (session == _uid_to_session.end()) {
    return;
  }
  for (auto slot = session->second.begin(); slot != session->second.end(); ++slot) {
    if (slot->second->GetSessionId() == session_id) {
      session->second.erase(slot);
      break;
    }
  }
  if (session->second.empty())
    _uid_to_session.erase(session);
}
