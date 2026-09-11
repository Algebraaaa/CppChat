#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
	bool AddFriendApply(int from_uid, int to_uid, const std::string& description, const std::string& remark);
	bool AuthFriendApply(int from_uid, int to_uid);
	bool AddFriend(
		int from_uid,
		int to_uid,
		const std::string& remark,
		std::vector<std::shared_ptr<ChatMessage>>& initial_messages);
	std::shared_ptr<UserInfo> GetUser(int uid);
	std::shared_ptr<UserInfo> GetUser(const std::string& name);
	bool GetApplyList(
		int to_uid,
		std::vector<std::shared_ptr<ApplyInfo>>& applications,
		int begin_id,
		int limit = 10);
	bool GetFriendList(int self_uid, std::vector<std::shared_ptr<UserInfo>>& friends);
	bool GetUserThreads(
		std::int64_t user_id,
		std::int64_t last_id,
		int page_size,
		std::vector<std::shared_ptr<ChatThreadInfo>>& threads,
		bool& load_more,
		int& next_last_id);
	bool CreatePrivateChat(int user1_uid, int user2_uid, int& thread_id);
	std::shared_ptr<PageResult> LoadChatMsg(
		int thread_id,
		int last_message_id,
		int page_size);
	bool AddChatMsg(std::vector<std::shared_ptr<ChatMessage>>& messages);
private:
	MysqlMgr();
	MysqlDao  _dao;
};
