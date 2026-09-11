#include "LogicSystem.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include <json/reader.h>

#include "CServer.h"
#include "CSession.h"
#include "ChatGrpcClient.h"
#include "ConfigMgr.h"
#include "Logger.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "StatusGrpcClient.h"
#include "UserMgr.h"
#include "const.h"
#include "utils.h"

namespace
{
	bool ParseRequest(const std::string& text, Json::Value& request)
	{
		Json::Reader reader;
		return reader.parse(text, request) && request.isObject();
	}

	void FillUserJson(const UserInfo& user, Json::Value& value)
	{
		value["uid"] = user.uid;
		value["name"] = user.name;
		value["email"] = user.email;
		value["nick"] = user.nick;
		value["desc"] = user.desc;
		value["sex"] = user.sex;
		value["icon"] = user.icon;
		value["back"] = user.back;
	}

	void FillChatJson(const ChatMessage& message, Json::Value& value)
	{
		value["message_id"] = message.message_id;
		value["thread_id"] = message.thread_id;
		value["sender"] = message.sender_id;
		value["recv_id"] = message.recv_id;
		value["unique_id"] = message.unique_id;
		value["content"] = message.content;
		value["msg_content"] = message.content;
		value["chat_time"] = message.chat_time;
		value["status"] = message.status;
	}
}

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
	server_.reset();
	LOG_INFO("Logic system worker stopped.");
}

void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> message)
{
	if (!message)
	{
		return;
	}
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopped_)
		{
			return;
		}
		message_queue_.push(std::move(message));
	}
	consume_condition_.notify_one();
}

void LogicSystem::SetServer(std::shared_ptr<CServer> server)
{
	server_ = std::move(server);
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
		callback->second(
			message->_session,
			message_id,
			std::string(message->_recvnode->_data, message->_recvnode->_cur_len));
	}
}

void LogicSystem::RegisterCallbacks()
{
	auto bind = [this](auto handler)
	{
		return std::bind(
			handler,
			this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3);
	};
	callbacks_[MSG_CHAT_LOGIN] = bind(&LogicSystem::LoginHandler);
	callbacks_[ID_SEARCH_USER_REQ] = bind(&LogicSystem::SearchInfo);
	callbacks_[ID_ADD_FRIEND_REQ] = bind(&LogicSystem::AddFriendApply);
	callbacks_[ID_AUTH_FRIEND_REQ] = bind(&LogicSystem::AuthFriendApply);
	callbacks_[ID_TEXT_CHAT_MSG_REQ] = bind(&LogicSystem::DealChatTextMsg);
	callbacks_[ID_HEART_BEAT_REQ] = bind(&LogicSystem::HeartBeatHandler);
	callbacks_[ID_LOAD_CHAT_THREAD_REQ] = bind(&LogicSystem::GetUserThreadsHandler);
	callbacks_[ID_CREATE_PRIVATE_CHAT_REQ] = bind(&LogicSystem::CreatePrivateChat);
	callbacks_[ID_LOAD_CHAT_MSG_REQ] = bind(&LogicSystem::LoadChatMsg);
}

void LogicSystem::LoginHandler(
	std::shared_ptr<CSession> session,
	short,
	const std::string& message_data)
{
	Json::Value request;
	Json::Value response;
	if (!ParseRequest(message_data, request) ||
		!request["uid"].isInt() || !request["token"].isString())
	{
		response["error"] = ErrorCodes::Error_Json;
		session->Send(response.toStyledString(), MSG_CHAT_LOGIN_RSP);
		return;
	}

	Defer send_response([&response, session]()
	{
		session->Send(response.toStyledString(), MSG_CHAT_LOGIN_RSP);
	});

	const int uid = request["uid"].asInt();
	const auto status_reply = StatusGrpcClient::GetInstance()->Login(
		uid, request["token"].asString());
	response["error"] = status_reply.error();
	if (status_reply.error() != ErrorCodes::Success)
	{
		return;
	}

	auto user = std::make_shared<UserInfo>();
	if (!GetBaseInfo(std::string(USER_BASE_INFO) + std::to_string(uid), uid, user))
	{
		response["error"] = ErrorCodes::UidInvalid;
		return;
	}
	FillUserJson(*user, response);
	response["token"] = status_reply.token();

	std::vector<std::shared_ptr<ApplyInfo>> applications;
	if (GetFriendApplyInfo(uid, applications))
	{
		for (const auto& application : applications)
		{
			Json::Value item;
			item["uid"] = application->_uid;
			item["name"] = application->_name;
			item["desc"] = application->_desc;
			item["icon"] = application->_icon;
			item["nick"] = application->_nick;
			item["sex"] = application->_sex;
			item["status"] = application->_status;
			response["apply_list"].append(item);
		}
	}

	std::vector<std::shared_ptr<UserInfo>> friends;
	if (GetFriendList(uid, friends))
	{
		for (const auto& friend_info : friends)
		{
			Json::Value item;
			FillUserJson(*friend_info, item);
			response["friend_list"].append(item);
		}
	}

	const std::string uid_text = std::to_string(uid);
	const std::string lock_key = std::string(LOCK_PREFIX) + uid_text;
	const std::string identifier = RedisMgr::GetInstance()->AcquireLock(
		lock_key, LOCK_TIME_OUT, ACQUIRE_TIME_OUT);
	if (identifier.empty())
	{
		response["error"] = ErrorCodes::RPCFailed;
		return;
	}
	Defer release_lock([&identifier, &lock_key]()
	{
		RedisMgr::GetInstance()->ReleaseLock(lock_key, identifier);
	});

	const std::string self_name = ConfigMgr::GetInstance()["SelfServer"]["Name"];
	std::string previous_server;
	if (RedisMgr::GetInstance()->Get(
		std::string(USER_IP_PREFIX) + uid_text, previous_server) &&
		previous_server == self_name)
	{
		auto old_session = UserMgr::GetInstance()->GetSession(uid);
		if (old_session && old_session->GetSessionId() != session->GetSessionId())
		{
			old_session->NotifyOffline(uid);
			old_session->Close();
			if (server_)
			{
				server_->ClearSession(old_session->GetSessionId());
			}
		}
	}

	session->SetUserId(uid);
	UserMgr::GetInstance()->SetUserSession(uid, session);
	RedisMgr::GetInstance()->Set(std::string(USER_IP_PREFIX) + uid_text, self_name);
	RedisMgr::GetInstance()->Set(
		std::string(USER_SESSION_PREFIX) + uid_text,
		session->GetSessionId());
	LOG_INFO("Chat user logged in: uid=", uid, ", session=", session->GetSessionId(), '.');
}

void LogicSystem::SearchInfo(
	std::shared_ptr<CSession> session,
	short,
	const std::string& message_data)
{
	Json::Value request;
	Json::Value response;
	Defer send_response([&response, session]()
	{
		session->Send(response.toStyledString(), ID_SEARCH_USER_RSP);
	});
	if (!ParseRequest(message_data, request) || request["uid"].isNull())
	{
		response["error"] = ErrorCodes::Error_Json;
		return;
	}

	const std::string query = request["uid"].isString()
		? request["uid"].asString()
		: std::to_string(request["uid"].asInt());
	if (IsPureDigit(query))
	{
		GetUserByUid(query, response);
	}
	else
	{
		GetUserByName(query, response);
	}
}

void LogicSystem::AddFriendApply(
	std::shared_ptr<CSession> session,
	short,
	const std::string& message_data)
{
	Json::Value request;
	Json::Value response;
	Defer send_response([&response, session]()
	{
		session->Send(response.toStyledString(), ID_ADD_FRIEND_RSP);
	});
	if (!ParseRequest(message_data, request) ||
		!request["uid"].isInt() || !request["touid"].isInt())
	{
		response["error"] = ErrorCodes::Error_Json;
		return;
	}

	const int uid = request["uid"].asInt();
	const int to_uid = request["touid"].asInt();
	const std::string description = request.get("applyname", "").asString();
	const std::string remark = request.get("bakname", "").asString();
	response["error"] = MysqlMgr::GetInstance()->AddFriendApply(
		uid, to_uid, description, remark)
		? ErrorCodes::Success : ErrorCodes::RPCFailed;
	if (response["error"].asInt() != ErrorCodes::Success)
	{
		return;
	}

	std::string destination;
	if (!RedisMgr::GetInstance()->Get(
		std::string(USER_IP_PREFIX) + std::to_string(to_uid), destination))
	{
		return;
	}

	auto applicant = std::make_shared<UserInfo>();
	GetBaseInfo(std::string(USER_BASE_INFO) + std::to_string(uid), uid, applicant);
	const std::string self_name = ConfigMgr::GetInstance()["SelfServer"]["Name"];
	if (destination == self_name)
	{
		auto target = UserMgr::GetInstance()->GetSession(to_uid);
		if (target)
		{
			Json::Value notification;
			notification["error"] = ErrorCodes::Success;
			notification["applyuid"] = uid;
			notification["desc"] = description;
			if (applicant)
			{
				FillUserJson(*applicant, notification);
			}
			target->Send(notification.toStyledString(), ID_NOTIFY_ADD_FRIEND_REQ);
		}
		return;
	}

	message::AddFriendReq grpc_request;
	grpc_request.set_applyuid(uid);
	grpc_request.set_touid(to_uid);
	grpc_request.set_name(applicant ? applicant->name : std::string());
	grpc_request.set_desc(description);
	ChatGrpcClient::GetInstance()->NotifyAddFriend(destination, grpc_request);
}

void LogicSystem::AuthFriendApply(
	std::shared_ptr<CSession> session,
	short,
	const std::string& message_data)
{
	Json::Value request;
	Json::Value response;
	Defer send_response([&response, session]()
	{
		session->Send(response.toStyledString(), ID_AUTH_FRIEND_RSP);
	});
	if (!ParseRequest(message_data, request) ||
		!request["fromuid"].isInt() || !request["touid"].isInt())
	{
		response["error"] = ErrorCodes::Error_Json;
		return;
	}

	const int from_uid = request["fromuid"].asInt();
	const int to_uid = request["touid"].asInt();
	std::vector<std::shared_ptr<ChatMessage>> initial_messages;
	if (!MysqlMgr::GetInstance()->AddFriend(
		from_uid, to_uid, request.get("back", "").asString(), initial_messages))
	{
		response["error"] = ErrorCodes::RPCFailed;
		return;
	}

	response["error"] = ErrorCodes::Success;
	response["uid"] = to_uid;
	auto accepted_user = std::make_shared<UserInfo>();
	if (GetBaseInfo(std::string(USER_BASE_INFO) + std::to_string(to_uid), to_uid, accepted_user))
	{
		FillUserJson(*accepted_user, response);
	}
	const std::string timestamp = GetCurrentTimestamp();
	for (const auto& message : initial_messages)
	{
		if (message->chat_time.empty())
		{
			message->chat_time = timestamp;
		}
		Json::Value value;
		FillChatJson(*message, value);
		response["chat_datas"].append(value);
	}

	std::string destination;
	if (!RedisMgr::GetInstance()->Get(
		std::string(USER_IP_PREFIX) + std::to_string(to_uid), destination))
	{
		return;
	}
	const std::string self_name = ConfigMgr::GetInstance()["SelfServer"]["Name"];
	if (destination == self_name)
	{
		auto target = UserMgr::GetInstance()->GetSession(to_uid);
		if (target)
		{
			Json::Value notification = response;
			notification["fromuid"] = from_uid;
			notification["touid"] = to_uid;
			target->Send(notification.toStyledString(), ID_NOTIFY_AUTH_FRIEND_REQ);
		}
		return;
	}

	message::AuthFriendReq grpc_request;
	grpc_request.set_fromuid(from_uid);
	grpc_request.set_touid(to_uid);
	ChatGrpcClient::GetInstance()->NotifyAuthFriend(destination, grpc_request);
}

void LogicSystem::DealChatTextMsg(
	std::shared_ptr<CSession> session,
	short,
	const std::string& message_data)
{
	Json::Value request;
	Json::Value response;
	Defer send_response([&response, session]()
	{
		session->Send(response.toStyledString(), ID_TEXT_CHAT_MSG_RSP);
	});
	if (!ParseRequest(message_data, request) ||
		!request["fromuid"].isInt() || !request["touid"].isInt() ||
		!request["thread_id"].isInt() || !request["text_array"].isArray())
	{
		response["error"] = ErrorCodes::Error_Json;
		return;
	}

	const int from_uid = request["fromuid"].asInt();
	const int to_uid = request["touid"].asInt();
	const int thread_id = request["thread_id"].asInt();
	response["fromuid"] = from_uid;
	response["touid"] = to_uid;
	response["thread_id"] = thread_id;

	std::vector<std::shared_ptr<ChatMessage>> messages;
	const std::string timestamp = GetCurrentTimestamp();
	for (const auto& item : request["text_array"])
	{
		if (!item["content"].isString())
		{
			continue;
		}
		auto message = std::make_shared<ChatMessage>();
		message->thread_id = thread_id;
		message->sender_id = from_uid;
		message->recv_id = to_uid;
		message->unique_id = item.get("unique_id", item.get("msgid", "")).asString();
		message->content = item["content"].asString();
		message->chat_time = timestamp;
		message->status = 2;
		messages.push_back(std::move(message));
	}
	if (messages.empty() || !MysqlMgr::GetInstance()->AddChatMsg(messages))
	{
		response["error"] = ErrorCodes::RPCFailed;
		return;
	}

	response["error"] = ErrorCodes::Success;
	for (const auto& message : messages)
	{
		Json::Value value;
		FillChatJson(*message, value);
		response["chat_datas"].append(value);
	}

	std::string destination;
	if (!RedisMgr::GetInstance()->Get(
		std::string(USER_IP_PREFIX) + std::to_string(to_uid), destination))
	{
		return;
	}
	const std::string self_name = ConfigMgr::GetInstance()["SelfServer"]["Name"];
	if (destination == self_name)
	{
		auto target = UserMgr::GetInstance()->GetSession(to_uid);
		if (target)
		{
			target->Send(response.toStyledString(), ID_NOTIFY_TEXT_CHAT_MSG_REQ);
		}
		return;
	}

	message::TextChatMsgReq grpc_request;
	grpc_request.set_fromuid(from_uid);
	grpc_request.set_touid(to_uid);
	for (const auto& message : messages)
	{
		auto* value = grpc_request.add_textmsgs();
		value->set_msgid(message->unique_id);
		value->set_msgcontent(message->content);
	}
	ChatGrpcClient::GetInstance()->NotifyTextChatMsg(destination, grpc_request, response);
}

void LogicSystem::HeartBeatHandler(
	std::shared_ptr<CSession> session,
	short,
	const std::string&)
{
	session->UpdateHeartbeat();
	Json::Value response;
	response["error"] = ErrorCodes::Success;
	response["uid"] = session->GetUserId();
	session->Send(response.toStyledString(), ID_HEARTBEAT_RSP);
}

bool LogicSystem::IsPureDigit(const std::string& text) const
{
	return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char value)
	{
		return std::isdigit(value) != 0;
	});
}

void LogicSystem::GetUserByUid(const std::string& uid_text, Json::Value& response)
{
	try
	{
		const int uid = std::stoi(uid_text);
		auto user = std::make_shared<UserInfo>();
		if (!GetBaseInfo(std::string(USER_BASE_INFO) + uid_text, uid, user))
		{
			response["error"] = ErrorCodes::UidInvalid;
			return;
		}
		response["error"] = ErrorCodes::Success;
		FillUserJson(*user, response);
	}
	catch (const std::exception&)
	{
		response["error"] = ErrorCodes::UidInvalid;
	}
}

void LogicSystem::GetUserByName(const std::string& name, Json::Value& response)
{
	std::string cached;
	const std::string key = std::string(NAME_INFO) + name;
	if (RedisMgr::GetInstance()->Get(key, cached))
	{
		Json::Value value;
		if (ParseRequest(cached, value))
		{
			response = value;
			response["error"] = ErrorCodes::Success;
			return;
		}
	}

	auto user = MysqlMgr::GetInstance()->GetUser(name);
	if (!user)
	{
		response["error"] = ErrorCodes::UidInvalid;
		return;
	}
	response["error"] = ErrorCodes::Success;
	FillUserJson(*user, response);
	Json::Value cache_value;
	FillUserJson(*user, cache_value);
	RedisMgr::GetInstance()->Set(key, cache_value.toStyledString());
}

bool LogicSystem::GetBaseInfo(
	const std::string& base_key,
	int uid,
	std::shared_ptr<UserInfo>& user_info)
{
	std::string cached;
	if (RedisMgr::GetInstance()->Get(base_key, cached))
	{
		Json::Value value;
		if (ParseRequest(cached, value))
		{
			user_info->uid = value["uid"].asInt();
			user_info->name = value["name"].asString();
			user_info->email = value["email"].asString();
			user_info->nick = value["nick"].asString();
			user_info->desc = value["desc"].asString();
			user_info->sex = value["sex"].asInt();
			user_info->icon = value["icon"].asString();
			return true;
		}
	}

	auto database_user = MysqlMgr::GetInstance()->GetUser(uid);
	if (!database_user)
	{
		return false;
	}
	user_info = std::move(database_user);
	Json::Value value;
	FillUserJson(*user_info, value);
	RedisMgr::GetInstance()->Set(base_key, value.toStyledString());
	return true;
}

bool LogicSystem::GetFriendApplyInfo(
	int to_uid,
	std::vector<std::shared_ptr<ApplyInfo>>& applications)
{
	return MysqlMgr::GetInstance()->GetApplyList(to_uid, applications, 0, 10);
}

bool LogicSystem::GetFriendList(
	int self_uid,
	std::vector<std::shared_ptr<UserInfo>>& friends)
{
	return MysqlMgr::GetInstance()->GetFriendList(self_uid, friends);
}

void LogicSystem::GetUserThreadsHandler(
	std::shared_ptr<CSession> session,
	short,
	const std::string& message_data)
{
	Json::Value request;
	Json::Value response;
	Defer send_response([&response, session]()
	{
		session->Send(response.toStyledString(), ID_LOAD_CHAT_THREAD_RSP);
	});
	if (!ParseRequest(message_data, request) || !request["uid"].isInt())
	{
		response["error"] = ErrorCodes::Error_Json;
		return;
	}

	const int uid = request["uid"].asInt();
	const int last_id = request.get("thread_id", 0).asInt();
	std::vector<std::shared_ptr<ChatThreadInfo>> threads;
	bool load_more = false;
	int next_last_id = 0;
	if (!MysqlMgr::GetInstance()->GetUserThreads(
		uid, last_id, 10, threads, load_more, next_last_id))
	{
		response["error"] = ErrorCodes::UidInvalid;
		return;
	}

	response["error"] = ErrorCodes::Success;
	response["uid"] = uid;
	response["load_more"] = load_more;
	response["next_last_id"] = next_last_id;
	for (const auto& thread : threads)
	{
		Json::Value value;
		value["thread_id"] = thread->_thread_id;
		value["type"] = thread->_type;
		value["user1_id"] = thread->_user1_id;
		value["user2_id"] = thread->_user2_id;
		response["threads"].append(value);
	}
}

void LogicSystem::CreatePrivateChat(
	std::shared_ptr<CSession> session,
	short,
	const std::string& message_data)
{
	Json::Value request;
	Json::Value response;
	Defer send_response([&response, session]()
	{
		session->Send(response.toStyledString(), ID_CREATE_PRIVATE_CHAT_RSP);
	});
	if (!ParseRequest(message_data, request) ||
		!request["uid"].isInt() || !request["other_id"].isInt())
	{
		response["error"] = ErrorCodes::Error_Json;
		return;
	}

	const int uid = request["uid"].asInt();
	const int other_uid = request["other_id"].asInt();
	int thread_id = 0;
	response["uid"] = uid;
	response["other_id"] = other_uid;
	response["error"] = MysqlMgr::GetInstance()->CreatePrivateChat(
		uid, other_uid, thread_id)
		? ErrorCodes::Success : ErrorCodes::CreateChatFailed;
	if (response["error"].asInt() == ErrorCodes::Success)
	{
		response["thread_id"] = thread_id;
	}
}

void LogicSystem::LoadChatMsg(
	std::shared_ptr<CSession> session,
	short,
	const std::string& message_data)
{
	Json::Value request;
	Json::Value response;
	Defer send_response([&response, session]()
	{
		session->Send(response.toStyledString(), ID_LOAD_CHAT_MSG_RSP);
	});
	if (!ParseRequest(message_data, request) || !request["thread_id"].isInt())
	{
		response["error"] = ErrorCodes::Error_Json;
		return;
	}

	const int thread_id = request["thread_id"].asInt();
	const int message_id = request.get("message_id", 0).asInt();
	const auto page = MysqlMgr::GetInstance()->LoadChatMsg(thread_id, message_id, 10);
	if (!page)
	{
		response["error"] = ErrorCodes::LoadChatFailed;
		return;
	}

	response["error"] = ErrorCodes::Success;
	response["thread_id"] = thread_id;
	response["last_message_id"] = page->next_cursor;
	response["load_more"] = page->load_more;
	for (const auto& message : page->messages)
	{
		Json::Value value;
		FillChatJson(message, value);
		response["chat_datas"].append(value);
	}
}
