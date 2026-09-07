#include "LogicSystem.h"

#include <functional>

#include <json/reader.h>
#include <json/value.h>

#include "CSession.h"
#include "Logger.h"
#include "StatusGrpcClient.h"
#include "const.h"

LogicSystem::LogicSystem()
{
	RegisterCallbacks();
	worker_thread_ = std::thread(&LogicSystem::DealMsg, this);
	LOG_INFO("Logic system worker started.");
}

LogicSystem::~LogicSystem()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stopped_ = true;
	}
	consume_condition_.notify_one();
	if (worker_thread_.joinable())
	{
		worker_thread_.join();
	}
	LOG_INFO("Logic system worker stopped.");
}

void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> message)
{
	if (!message)
	{
		LOG_WARNING("Ignored an empty logic message.");
		return;
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopped_)
		{
			LOG_WARNING("Ignored a logic message while stopping.");
			return;
		}
		message_queue_.push(std::move(message));
	}
	consume_condition_.notify_one();
}

void LogicSystem::DealMsg()
{
	for (;;)
	{
		std::shared_ptr<LogicNode> message;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			consume_condition_.wait(lock, [this]()
			{
				return stopped_ || !message_queue_.empty();
			});

			if (stopped_ && message_queue_.empty())
			{
				return;
			}

			message = std::move(message_queue_.front());
			message_queue_.pop();
		}

		const short message_id = message->_recvnode->_msg_id;
		const auto callback = callbacks_.find(message_id);
		if (callback == callbacks_.end())
		{
			LOG_WARNING("No callback registered for message id ", message_id, '.');
			continue;
		}

		const std::string message_data(
			message->_recvnode->_data,
			message->_recvnode->_cur_len);
		callback->second(message->_session, message_id, message_data);
	}
}

void LogicSystem::RegisterCallbacks()
{
	callbacks_[MSG_CHAT_LOGIN] = std::bind(
		&LogicSystem::LoginHandler,
		this,
		std::placeholders::_1,
		std::placeholders::_2,
		std::placeholders::_3);
}

void LogicSystem::LoginHandler(
	std::shared_ptr<CSession> session,
	short message_id,
	const std::string& message_data)
{
	Json::Value request_json;
	Json::Value response_json;
	Json::Reader reader;

	if (!reader.parse(message_data, request_json) ||
		!request_json.isMember("uid") || !request_json["uid"].isInt() ||
		!request_json.isMember("token") || !request_json["token"].isString())
	{
		LOG_WARNING("Chat login contains invalid JSON or missing fields.");
		response_json["error"] = ErrorCodes::Error_Json;
		session->Send(response_json.toStyledString(), MSG_CHAT_LOGIN_RSP);
		return;
	}

	const int uid = request_json["uid"].asInt();
	LOG_INFO("Chat login request received: uid=", uid,
		", message_id=", message_id, '.');

	// 把 GateServer 签发的 uid/token 交给 StatusServer 校验；token 不写入日志。
	const auto status_reply = StatusGrpcClient::GetInstance()->Login(
		uid,
		request_json["token"].asString());
	response_json["error"] = status_reply.error();
	if (status_reply.error() != ErrorCodes::Success)
	{
		LOG_WARNING("Chat login rejected by StatusServer: uid=", uid,
			", error=", status_reply.error(), '.');
		session->Send(response_json.toStyledString(), MSG_CHAT_LOGIN_RSP);
		return;
	}

	response_json["uid"] = status_reply.uid();
	response_json["token"] = status_reply.token();
	response_json["name"] = request_json.get("name", "").asString();
	session->Send(response_json.toStyledString(), MSG_CHAT_LOGIN_RSP);
}
