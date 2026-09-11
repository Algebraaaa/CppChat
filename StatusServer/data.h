#pragma once

#include <string>
#include <utility>
#include <vector>

// 数据库层与业务层之间传递的普通数据对象。
// 成员在声明时给出默认值，避免使用未初始化的整数。
struct UserInfo
{
	std::string name;
	std::string pwd;
	int uid = 0;
	std::string email;
	std::string nick;
	std::string desc;
	int sex = 0;
	std::string icon;
	std::string back;
};

struct ApplyInfo
{
	ApplyInfo(int uid, std::string name, std::string desc, std::string icon,
		std::string nick, int sex, int status)
		: _uid(uid), _name(std::move(name)), _desc(std::move(desc)),
		_icon(std::move(icon)), _nick(std::move(nick)), _sex(sex), _status(status)
	{
	}

	int _uid = 0;
	std::string _name;
	std::string _desc;
	std::string _icon;
	std::string _nick;
	int _sex = 0;
	int _status = 0;
};

struct ChatThreadInfo
{
	int _thread_id = 0;
	std::string _type;
	int _user1_id = 0;
	int _user2_id = 0;
};

struct ChatMessage
{
	int message_id = 0;
	int thread_id = 0;
	int sender_id = 0;
	int recv_id = 0;
	std::string unique_id;
	std::string content;
	std::string chat_time;
	int status = 0;
};

struct PageResult
{
	std::vector<ChatMessage> messages;
	bool load_more = false;
	int next_cursor = 0;
};
