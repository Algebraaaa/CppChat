#pragma once

#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "Singleton.h"

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

private:
	LogicSystem();

	void DealMsg();
	void RegisterCallbacks();
	void LoginHandler(
		std::shared_ptr<CSession> session,
		short message_id,
		const std::string& message_data);

	std::thread worker_thread_;
	std::queue<std::shared_ptr<LogicNode>> message_queue_;
	std::mutex mutex_;
	std::condition_variable consume_condition_;
	bool stopped_ = false;
	std::map<short, MessageCallback> callbacks_;
};
