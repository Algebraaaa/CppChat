#include "ChatGrpcClient.h"
#include "RedisMgr.h"

ChatGrpcClient::ChatGrpcClient()
{
	auto& config = ConfigMgr::GetInstance();
	auto configured_server_names = config["Chatservers"]["Name"];

	std::vector<std::string> server_section_names;

	std::stringstream server_names_stream(configured_server_names);
	std::string server_section_name;

	while (std::getline(server_names_stream, server_section_name, ',')) {
		server_section_names.push_back(server_section_name);
	}

	for (auto& server_section_name : server_section_names) {
		if (config[server_section_name]["Name"].empty()) {
			continue;
		}

		chat_stub_pools_[config[server_section_name]["Name"]] = std::make_unique<ChatStubPool>(5, config[server_section_name]["Host"], config[server_section_name]["Port"]);
	}

}

AddFriendRsp ChatGrpcClient::NotifyAddFriend(const AddFriendReq& req)
{
	auto to_uid = req.touid();
	std::string  uid_str = std::to_string(to_uid);

	AddFriendRsp rsp;
	return rsp;
}
