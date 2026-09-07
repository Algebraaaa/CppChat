#pragma once

#include <cstring>

#include "const.h"

class LogicSystem;

class MsgNode
{
public:
	explicit MsgNode(short max_length);
	virtual ~MsgNode();

	MsgNode(const MsgNode&) = delete;
	MsgNode& operator=(const MsgNode&) = delete;

	void Clear();

	short _cur_len;
	short _total_len;
	char* _data;
};

class RecvNode : public MsgNode
{
	friend class LogicSystem;

public:
	RecvNode(short max_length, short message_id);

private:
	short _msg_id;
};

class SendNode : public MsgNode
{
	friend class LogicSystem;

public:
	SendNode(const char* message, short message_length, short message_id);

private:
	short _msg_id;
};
