#include "MsgNode.h"

#include <stdexcept>

#include <boost/asio/detail/socket_ops.hpp>

MsgNode::MsgNode(short max_length)
	: _cur_len(0),
	  _total_len(max_length),
	  _data(nullptr)
{
	if (max_length <= 0)
	{
		throw std::invalid_argument("message node length must be positive");
	}
	_data = new char[static_cast<std::size_t>(_total_len) + 1]();
	_data[_total_len] = '\0';
}

MsgNode::~MsgNode()
{
	delete[] _data;
}

void MsgNode::Clear()
{
	std::memset(_data, 0, static_cast<std::size_t>(_total_len));
	_cur_len = 0;
}

RecvNode::RecvNode(short max_length, short message_id)
	: MsgNode(max_length),
	  _msg_id(message_id)
{
}

SendNode::SendNode(
	const char* message,
	short message_length,
	short message_id)
	: MsgNode(static_cast<short>(message_length + HEAD_TOTAL_LEN)),
	  _msg_id(message_id)
{
	const short network_message_id =
		boost::asio::detail::socket_ops::host_to_network_short(message_id);
	std::memcpy(_data, &network_message_id, HEAD_ID_LEN);

	const short network_message_length =
		boost::asio::detail::socket_ops::host_to_network_short(message_length);
	std::memcpy(_data + HEAD_ID_LEN, &network_message_length, HEAD_DATA_LEN);
	std::memcpy(_data + HEAD_TOTAL_LEN, message, message_length);
}
