#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <json/value.h>

#include "Singleton.h"
#include "data.h"

class CServer;
class CSession;
class LogicNode;

using MessageCallback = std::function<void(
	std::shared_ptr<CSession>, short, const std::string&)>;

class LogicSystem : public Singleton<LogicSystem>
{
	friend class Singleton<LogicSystem>;

public:
	~LogicSystem();
	void PostMsgToQue(std::shared_ptr<LogicNode> message);
	void SetServer(std::shared_ptr<CServer> server);

private:
	LogicSystem();

	void DealMsg();
	void RegisterCallbacks();
	void LoginHandler(std::shared_ptr<CSession> session, short message_id, const std::string& message_data);
	void SearchInfo(std::shared_ptr<CSession> session, short message_id, const std::string& message_data);
	void AddFriendApply(std::shared_ptr<CSession> session, short message_id, const std::string& message_data);
	void AuthFriendApply(std::shared_ptr<CSession> session, short message_id, const std::string& message_data);
	void DealChatTextMsg(std::shared_ptr<CSession> session, short message_id, const std::string& message_data);
	void HeartBeatHandler(std::shared_ptr<CSession> session, short message_id, const std::string& message_data);
	void GetUserThreadsHandler(std::shared_ptr<CSession> session, short message_id, const std::string& message_data);
	void CreatePrivateChat(std::shared_ptr<CSession> session, short message_id, const std::string& message_data);
	void LoadChatMsg(std::shared_ptr<CSession> session, short message_id, const std::string& message_data);

	bool IsPureDigit(const std::string& text) const;
	void GetUserByUid(const std::string& uid_text, Json::Value& response);
	void GetUserByName(const std::string& name, Json::Value& response);
	bool GetBaseInfo(const std::string& base_key, int uid, std::shared_ptr<UserInfo>& user_info);
	bool GetFriendApplyInfo(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& applications);
	bool GetFriendList(int self_uid, std::vector<std::shared_ptr<UserInfo>>& friends);

	std::thread worker_thread_;
	std::queue<std::shared_ptr<LogicNode>> message_queue_;
	std::mutex mutex_;
	std::condition_variable consume_condition_;
	bool stopped_ = false;
	std::map<short, MessageCallback> callbacks_;
	std::shared_ptr<CServer> server_;
};
