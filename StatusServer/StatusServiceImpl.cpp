#include "StatusServiceImpl.h"
#include "ConfigMgr.h"
#include "const.h"
#include "Logger.h"

#include <atomic>
#include <array>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <cstdint>

namespace {
// 给每次 GetChatServer 调用分配一个递增编号，方便把同一次请求的多条日志串起来。
std::atomic<std::uint64_t> g_next_request_id{ 0 };

std::string GenerateUniqueString() {
	// 创建UUID对象
	boost::uuids::uuid uuid = boost::uuids::random_generator()();

	// 将UUID转换为字符串
	std::string unique_string = to_string(uuid);

	return unique_string;
}
} // namespace

Status StatusServiceImpl::GetChatServer(ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply)
{
	const std::uint64_t request_id =
		g_next_request_id.fetch_add(1, std::memory_order_relaxed) + 1;
	const int uid = request == nullptr ? 0 : request->uid();
	const std::string peer = context == nullptr ? "unknown" : context->peer();

	LOG_INFO("GetChatServer request received: request_id=", request_id,
		", uid=", uid, ", peer=", peer);

	if (request == nullptr || reply == nullptr || request->uid() <= 0) {
		if (reply != nullptr) {
			reply->set_error(ErrorCodes::UidInvalid);
		}
		LOG_WARNING("GetChatServer request rejected: request_id=", request_id,
			", uid=", uid, ", reason=invalid request or uid");
		return Status::OK;
	}

	const ChatServer server = getChatServer();
	if (server.host.empty() || server.port.empty()) {
		reply->set_error(ErrorCodes::RPCFailed);
		LOG_ERROR("GetChatServer allocation failed: request_id=", request_id,
			", uid=", uid, ", reason=no available chat server");
		return Status::OK;
	}

	reply->set_host(server.host);
	reply->set_port(server.port);
	reply->set_error(ErrorCodes::Success);
	reply->set_token(GenerateUniqueString());
	insertToken(request->uid(), reply->token());

	LOG_INFO("GetChatServer allocation succeeded: request_id=", request_id,
		", uid=", uid,
		", server_name=", server.name,
		", endpoint=", server.host, ":", server.port,
		", assigned_count=", server.con_count,
		", error=", reply->error(),
		", token_generated=", !reply->token().empty());
	return Status::OK;
}

StatusServiceImpl::StatusServiceImpl()
{
	auto& cfg = ConfigMgr::GetInstance();
	const std::array<const char*, 2> section_names = { "ChatServer1", "ChatServer2" };
	LOG_INFO("Loading chat server configuration: candidate_sections=", section_names.size());

	for (const char* section_name : section_names) {
		const auto section = cfg[section_name];
		if (section.GetValue("Enabled", "true") != "true") {
			LOG_INFO("Chat server skipped: section=", section_name,
				", reason=disabled");
			continue;
		}

		ChatServer server;
		server.name = section.GetValue("Name", section_name);
		server.host = section["Host"];
		server.port = section["Port"];
		if (server.name.empty() || server.host.empty() || server.port.empty()) {
			LOG_WARNING("Chat server skipped: section=", section_name,
				", reason=incomplete configuration",
				", has_name=", !server.name.empty(),
				", has_host=", !server.host.empty(),
				", has_port=", !server.port.empty());
			continue;
		}

		_servers[server.name] = server;
		LOG_INFO("Chat server registered: section=", section_name,
			", server_name=", server.name,
			", endpoint=", server.host, ":", server.port,
			", initial_assigned_count=", server.con_count);
	}

	LOG_INFO("Chat server configuration loaded: registered_count=", _servers.size());
}

ChatServer StatusServiceImpl::getChatServer() {
	std::lock_guard<std::mutex> guard(_server_mtx);
	if (_servers.empty()) {
		LOG_ERROR("Chat server selection failed: registered_count=0");
		return {};
	}

	auto min_server = _servers.begin();
	// 逐个打印候选服务器，测试时可以直接观察选择依据。
	for (auto server = _servers.begin(); server != _servers.end(); ++server) {
		LOG_DEBUG("Chat server candidate: server_name=", server->second.name,
			", endpoint=", server->second.host, ":", server->second.port,
			", assigned_count=", server->second.con_count);
		if (server->second.con_count < min_server->second.con_count) {
			min_server = server;
		}
	}

	const int count_before = min_server->second.con_count;
	++min_server->second.con_count;
	LOG_INFO("Chat server selected: server_name=", min_server->second.name,
		", endpoint=", min_server->second.host, ":", min_server->second.port,
		", assigned_count_before=", count_before,
		", assigned_count_after=", min_server->second.con_count);
	return min_server->second;
}

Status StatusServiceImpl::Login(ServerContext* context, const LoginReq* request, LoginRsp* reply)
{
	(void)context;
	if (request == nullptr || reply == nullptr || request->uid() <= 0) {
		if (reply != nullptr) {
			reply->set_error(ErrorCodes::UidInvalid);
		}
		return Status::OK;
	}

	auto uid = request->uid();
	auto token = request->token();
	if (token.empty()) {
		reply->set_error(ErrorCodes::TokenInvalid);
		return Status::OK;
	}

	std::lock_guard<std::mutex> guard(_token_mtx);
	auto iter = _tokens.find(uid);
	if (iter == _tokens.end()) {
		reply->set_error(ErrorCodes::UidInvalid);
		return Status::OK;
	}
	if (iter->second != token) {
		reply->set_error(ErrorCodes::TokenInvalid);
		return Status::OK;
	}
	reply->set_error(ErrorCodes::Success);
	reply->set_uid(uid);
	reply->set_token(token);
	return Status::OK;
}

void StatusServiceImpl::insertToken(int uid, const std::string& token)
{
	std::lock_guard<std::mutex> guard(_token_mtx);
	_tokens[uid] = token;
}
