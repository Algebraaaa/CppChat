#include "StatusServiceImpl.h"
#include "ConfigMgr.h"
#include "const.h"

#include <array>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace {
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
	(void)context;
	if (request == nullptr || reply == nullptr || request->uid() <= 0) {
		if (reply != nullptr) {
			reply->set_error(ErrorCodes::UidInvalid);
		}
		return Status::OK;
	}

	const auto& server = getChatServer();
	if (server.host.empty() || server.port.empty()) {
		reply->set_error(ErrorCodes::RPCFailed);
		return Status::OK;
	}

	reply->set_host(server.host);
	reply->set_port(server.port);
	reply->set_error(ErrorCodes::Success);
	reply->set_token(GenerateUniqueString());
	insertToken(request->uid(), reply->token());
	return Status::OK;
}

StatusServiceImpl::StatusServiceImpl()
{
	auto& cfg = ConfigMgr::Inst();
	const std::array<const char*, 2> section_names = { "ChatServer1", "ChatServer2" };

	for (const char* section_name : section_names) {
		const auto section = cfg[section_name];
		if (section.GetValue("Enabled", "true") != "true") {
			continue;
		}

		ChatServer server;
		server.name = section.GetValue("Name", section_name);
		server.host = section["Host"];
		server.port = section["Port"];
		if (server.name.empty() || server.host.empty() || server.port.empty()) {
			continue;
		}

		_servers[server.name] = server;
	}
}

ChatServer StatusServiceImpl::getChatServer() {
	std::lock_guard<std::mutex> guard(_server_mtx);
	if (_servers.empty()) {
		return {};
	}

	auto min_server = _servers.begin();
	// 使用范围基于for循环
	for (auto server = _servers.begin(); server != _servers.end(); ++server) {
		if (server->second.con_count < min_server->second.con_count) {
			min_server = server;
		}
	}

	++min_server->second.con_count;
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
