#include "usermgr.h"
#include <QFileInfo>
#include <QStandardPaths>

void UserMgr::Reset()
{
  _user_info.reset();
  _token.clear();
  _apply_list.clear();
  _outgoing_apply_map.clear();
  _friend_list.clear();
  _friend_map.clear();
  _chat_map.clear();
  _uid_to_thread_id.clear();
}

void UserMgr::SetUserInfo(std::shared_ptr<UserInfo> user)
{
  _user_info = user;
  if (!user) return;
  const auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
      + QStringLiteral("/avatars/%1.png").arg(user->_uid);
  if (QFileInfo::exists(path)) user->_icon = path;
}

int UserMgr::GetUid() const { return _user_info ? _user_info->_uid : 0; }
QString UserMgr::GetName() const { return _user_info ? _user_info->_name : QString(); }
QString UserMgr::GetNick() const { return _user_info ? _user_info->_nick : QString(); }
QString UserMgr::GetIcon() const { return _user_info ? _user_info->_icon : QString(); }
QString UserMgr::GetDesc() const { return _user_info ? _user_info->_desc : QString(); }

void UserMgr::AppendApplyList(QJsonArray array)
{
  for (const auto &value : array) {
    const auto o = value.toObject();
    AddApplyList(std::make_shared<ApplyInfo>(o["uid"].toInt(), o["name"].toString(),
        o["desc"].toString(), o["icon"].toString(), o["nick"].toString(),
        o["sex"].toInt(), o["status"].toInt()));
  }
}

void UserMgr::AppendFriendList(QJsonArray array)
{
  for (const auto &value : array) {
    const auto o = value.toObject();
    auto user = std::make_shared<UserInfo>(o["uid"].toInt(), o["name"].toString(),
        o["nick"].toString(), o["icon"].toString(), o["sex"].toInt(), "", o["desc"].toString());
    user->_back = o["back"].toString();
    AddFriend(user, false);
  }
}

void UserMgr::AddApplyList(std::shared_ptr<ApplyInfo> apply)
{
  if (!apply || apply->_uid <= 0) return;
  for (auto &old : _apply_list) {
    if (old->_uid == apply->_uid) { old = apply; return; }
  }
  _apply_list.push_back(apply);
}

bool UserMgr::AlreadyApply(int uid) const
{
  for (const auto &apply : _apply_list) if (apply->_uid == uid) return true;
  return false;
}

void UserMgr::MarkApplyAccepted(int uid)
{
  for (auto &apply : _apply_list) if (apply->_uid == uid) apply->_status = 1;
}

void UserMgr::RememberOutgoingApply(std::shared_ptr<SearchInfo> user)
{
  if (user && user->_uid > 0) _outgoing_apply_map.insert(user->_uid, user);
}

std::shared_ptr<SearchInfo> UserMgr::TakeOutgoingApply(int uid)
{
  return _outgoing_apply_map.take(uid);
}

void UserMgr::AddFriend(std::shared_ptr<UserInfo> user, bool addToFront)
{
  if (!user || user->_uid <= 0) return;
  // 更新现有对象，让详情页持有的 shared_ptr 同时看到新资料。
  if (_friend_map.contains(user->_uid)) {
    auto old = _friend_map.value(user->_uid);
    const QString back = old->_back;
    *old = *user;
    if (old->_back.isEmpty()) old->_back = back;
    return;
  }
  _friend_map.insert(user->_uid, user);
  if (addToFront)
    _friend_list.insert(_friend_list.begin(), user);
  else
    _friend_list.push_back(user);
}
void UserMgr::AddFriend(std::shared_ptr<AuthRsp> auth) { AddFriend(std::make_shared<UserInfo>(auth)); }
void UserMgr::AddFriend(std::shared_ptr<AuthInfo> auth) { AddFriend(std::make_shared<UserInfo>(auth)); }
std::shared_ptr<UserInfo> UserMgr::GetFriendById(int uid) const { return _friend_map.value(uid); }

void UserMgr::AddChatThreadData(std::shared_ptr<ChatThreadData> data, int otherUid)
{
  if (!data || data->GetThreadId() <= 0) return;
  if (!_chat_map.contains(data->GetThreadId())) _chat_map.insert(data->GetThreadId(), data);
  if (otherUid > 0) _uid_to_thread_id.insert(otherUid, data->GetThreadId());
}
std::shared_ptr<ChatThreadData> UserMgr::GetChatThreadByThreadId(int threadId) const
{ return _chat_map.value(threadId); }
std::shared_ptr<ChatThreadData> UserMgr::GetChatThreadByUid(int uid) const
{ return _chat_map.value(_uid_to_thread_id.value(uid, 0)); }
