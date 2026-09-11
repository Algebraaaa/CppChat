#include "MysqlMgr.h"


MysqlMgr::~MysqlMgr() {

}

int MysqlMgr::RegUser(const std::string& name, const std::string& email, const std::string& password)
{
    return _dao.RegUser(name, email, password);
}

MysqlMgr::MysqlMgr() {
}
bool MysqlMgr::CheckEmail(const std::string& name, const std::string& email) {
  return _dao.CheckEmail(name, email);
}

bool MysqlMgr::UpdatePwd(const std::string& name, const std::string& new_password) {
  return _dao.UpdatePwd(name, new_password);
}
bool MysqlMgr::CheckPwdByEmail(const std::string& email, const std::string& password, UserInfo& user_info) {
	return _dao.CheckPwdByEmail(email, password, user_info);
}

bool MysqlMgr::AddFriendApply(
	int from_uid,
	int to_uid,
	const std::string& description,
	const std::string& remark)
{
	return _dao.AddFriendApply(from_uid, to_uid, description, remark);
}

bool MysqlMgr::AuthFriendApply(int from_uid, int to_uid)
{
	return _dao.AuthFriendApply(from_uid, to_uid);
}

bool MysqlMgr::AddFriend(
	int from_uid,
	int to_uid,
	const std::string& remark,
	std::vector<std::shared_ptr<ChatMessage>>& initial_messages)
{
	return _dao.AddFriend(from_uid, to_uid, remark, initial_messages);
}

std::shared_ptr<UserInfo> MysqlMgr::GetUser(int uid)
{
	return _dao.GetUser(uid);
}

std::shared_ptr<UserInfo> MysqlMgr::GetUser(const std::string& name)
{
	return _dao.GetUser(name);
}

bool MysqlMgr::GetApplyList(
	int to_uid,
	std::vector<std::shared_ptr<ApplyInfo>>& applications,
	int begin_id,
	int limit)
{
	return _dao.GetApplyList(to_uid, applications, begin_id, limit);
}

bool MysqlMgr::GetFriendList(
	int self_uid,
	std::vector<std::shared_ptr<UserInfo>>& friends)
{
	return _dao.GetFriendList(self_uid, friends);
}

bool MysqlMgr::GetUserThreads(
	std::int64_t user_id,
	std::int64_t last_id,
	int page_size,
	std::vector<std::shared_ptr<ChatThreadInfo>>& threads,
	bool& load_more,
	int& next_last_id)
{
	return _dao.GetUserThreads(
		user_id, last_id, page_size, threads, load_more, next_last_id);
}

bool MysqlMgr::CreatePrivateChat(
	int user1_uid,
	int user2_uid,
	int& thread_id)
{
	return _dao.CreatePrivateChat(user1_uid, user2_uid, thread_id);
}

std::shared_ptr<PageResult> MysqlMgr::LoadChatMsg(
	int thread_id,
	int last_message_id,
	int page_size)
{
	return _dao.LoadChatMsg(thread_id, last_message_id, page_size);
}

bool MysqlMgr::AddChatMsg(std::vector<std::shared_ptr<ChatMessage>>& messages)
{
	return _dao.AddChatMsg(messages);
}
