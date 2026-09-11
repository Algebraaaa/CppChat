#include "CSession.h"

#include <cstring>
#include <limits>
#include <utility>

#include <json/value.h>

#include "CServer.h"
#include "Logger.h"
#include "LogicSystem.h"
#include "RedisMgr.h"

CSession::CSession(boost::asio::io_context& io_context, CServer* server)
	: socket_(io_context),
	  uuid_(boost::uuids::to_string(boost::uuids::random_generator()())),
	  server_(server),
	  last_heartbeat_(std::time(nullptr)),
	  receive_header_node_(std::make_shared<MsgNode>(
		  static_cast<short>(HEAD_TOTAL_LEN)))
{
}

CSession::~CSession()
{
	LOG_DEBUG("TCP session destroyed: uuid=", uuid_, '.');
}

boost::asio::ip::tcp::socket& CSession::GetSocket()
{
	return socket_;
}

const std::string& CSession::GetUuid() const
{
	return uuid_;
}

const std::string& CSession::GetSessionId() const
{
	return uuid_;
}

void CSession::SetUserId(int uid)
{
	user_id_ = uid;
}

int CSession::GetUserId() const
{
	return user_id_.load();
}

void CSession::Start()
{
	AsyncReadHead();
}

void CSession::Send(const std::string& message, short message_id)
{
	if (message.empty() || message.size() > MAX_LENGTH ||
		message.size() > static_cast<std::size_t>(std::numeric_limits<short>::max()))
	{
		LOG_WARNING("Rejected outbound message: uuid=", uuid_,
			", message_id=", message_id,
			", bytes=", message.size(), '.');
		return;
	}

	Send(message.data(), static_cast<short>(message.size()), message_id);
}

void CSession::Send(
	const char* message,
	short message_length,
	short message_id)
{
	if (message == nullptr || message_length <= 0 ||
		static_cast<std::size_t>(message_length) > MAX_LENGTH)
	{
		LOG_WARNING("Rejected invalid outbound message: uuid=", uuid_, '.');
		return;
	}

	std::lock_guard<std::mutex> lock(send_mutex_);
	if (closed_)
	{
		return;
	}
	if (send_queue_.size() >= MAX_SENDQUE)
	{
		LOG_WARNING("Send queue is full: uuid=", uuid_,
			", limit=", MAX_SENDQUE, '.');
		return;
	}

	const bool write_in_progress = !send_queue_.empty();
	send_queue_.push(std::make_shared<SendNode>(
		message, message_length, message_id));
	if (write_in_progress)
	{
		return;
	}

	auto self = shared_from_this();
	const auto& message_node = send_queue_.front();
	boost::asio::async_write(
		socket_,
		boost::asio::buffer(message_node->_data, message_node->_total_len),
		[self](const boost::system::error_code& error, std::size_t)
		{
			self->HandleWrite(error);
		});
}

void CSession::Close()
{
	if (closed_.exchange(true))
	{
		return;
	}

	boost::system::error_code ignored_error;
	socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_error);
	socket_.close(ignored_error);
}

void CSession::AsyncReadBody(std::size_t total_length)
{
	auto self = shared_from_this();
	AsyncReadFull(total_length, [self, total_length](
		const boost::system::error_code& error,
		std::size_t bytes_transferred)
	{
		if (error)
		{
			self->HandleReadFailure("body", error);
			return;
		}
		if (bytes_transferred != total_length)
		{
			LOG_ERROR("Body length mismatch: uuid=", self->uuid_,
				", received=", bytes_transferred,
				", expected=", total_length, '.');
			self->Close();
			self->DealExceptionSession();
			return;
		}

		std::memcpy(
			self->receive_message_node_->_data,
			self->data_.data(),
			bytes_transferred);
		self->receive_message_node_->_cur_len =
			static_cast<short>(bytes_transferred);
		self->receive_message_node_->_data[total_length] = '\0';

		LOG_DEBUG("Received TCP message: uuid=", self->uuid_,
			", bytes=", bytes_transferred, '.');
		self->UpdateHeartbeat();
		LogicSystem::GetInstance()->PostMsgToQue(std::make_shared<LogicNode>(
			self,
			self->receive_message_node_));
		self->AsyncReadHead();
	});
}

void CSession::AsyncReadHead()
{
	auto self = shared_from_this();
	AsyncReadFull(HEAD_TOTAL_LEN, [self](
		const boost::system::error_code& error,
		std::size_t bytes_transferred)
	{
		if (error)
		{
			self->HandleReadFailure("header", error);
			return;
		}
		if (bytes_transferred != HEAD_TOTAL_LEN)
		{
			LOG_ERROR("Header length mismatch: uuid=", self->uuid_, '.');
			self->Close();
			self->DealExceptionSession();
			return;
		}

		self->receive_header_node_->Clear();
		std::memcpy(
			self->receive_header_node_->_data,
			self->data_.data(),
			HEAD_TOTAL_LEN);

		short message_id = 0;
		std::memcpy(&message_id, self->receive_header_node_->_data, HEAD_ID_LEN);
		message_id = boost::asio::detail::socket_ops::network_to_host_short(message_id);

		short message_length = 0;
		std::memcpy(
			&message_length,
			self->receive_header_node_->_data + HEAD_ID_LEN,
			HEAD_DATA_LEN);
		message_length =
			boost::asio::detail::socket_ops::network_to_host_short(message_length);

		if (message_id <= 0 || message_length <= 0 ||
			static_cast<std::size_t>(message_length) > MAX_LENGTH)
		{
			LOG_WARNING("Invalid TCP header: uuid=", self->uuid_,
				", message_id=", message_id,
				", message_length=", message_length, '.');
			self->Close();
			self->DealExceptionSession();
			return;
		}

		self->receive_message_node_ =
			std::make_shared<RecvNode>(message_length, message_id);
		self->AsyncReadBody(static_cast<std::size_t>(message_length));
	});
}

void CSession::AsyncReadFull(std::size_t total_length, ReadHandler handler)
{
	data_.fill(0);
	AsyncReadLength(0, total_length, std::move(handler));
}

void CSession::AsyncReadLength(
	std::size_t read_length,
	std::size_t total_length,
	ReadHandler handler)
{
	auto self = shared_from_this();
	socket_.async_read_some(
		boost::asio::buffer(
			data_.data() + read_length,
			total_length - read_length),
		[read_length, total_length, handler = std::move(handler), self](
			const boost::system::error_code& error,
			std::size_t bytes_transferred) mutable
		{
			const std::size_t completed_length = read_length + bytes_transferred;
			if (error || completed_length >= total_length)
			{
				handler(error, completed_length);
				return;
			}

			self->AsyncReadLength(
				completed_length,
				total_length,
				std::move(handler));
		});
}

void CSession::HandleWrite(const boost::system::error_code& error)
{
	std::lock_guard<std::mutex> lock(send_mutex_);
	if (error)
	{
		LOG_ERROR("TCP write failed: uuid=", uuid_,
			", error=", error.message());
		while (!send_queue_.empty())
		{
			send_queue_.pop();
		}
		Close();
		DealExceptionSession();
		return;
	}

	send_queue_.pop();
	if (send_queue_.empty())
	{
		return;
	}

	auto self = shared_from_this();
	const auto& message_node = send_queue_.front();
	boost::asio::async_write(
		socket_,
		boost::asio::buffer(message_node->_data, message_node->_total_len),
		[self](const boost::system::error_code& next_error, std::size_t)
		{
			self->HandleWrite(next_error);
		});
}

void CSession::HandleReadFailure(
	const char* stage,
	const boost::system::error_code& error)
{
	if (error != boost::asio::error::operation_aborted &&
		error != boost::asio::error::eof)
	{
		LOG_ERROR("TCP ", stage, " read failed: uuid=", uuid_,
			", error=", error.message());
	}
	else
	{
		LOG_DEBUG("TCP session closed while reading ", stage,
			": uuid=", uuid_, '.');
	}
	Close();
	DealExceptionSession();
}

void CSession::NotifyOffline(int uid)
{
	Json::Value response;
	response["error"] = ErrorCodes::Success;
	response["uid"] = uid;
	Send(response.toStyledString(), ID_NOTIFY_OFF_LINE_REQ);
}

bool CSession::IsHeartbeatExpired(std::time_t now) const
{
	return std::difftime(now, last_heartbeat_.load()) > HEARTBEAT_TIMEOUT_SECONDS;
}

void CSession::UpdateHeartbeat()
{
	last_heartbeat_ = std::time(nullptr);
}

void CSession::DealExceptionSession()
{
	if (cleanup_started_.exchange(true))
	{
		return;
	}

	const int uid = GetUserId();
	if (uid > 0)
	{
		const std::string uid_text = std::to_string(uid);
		const std::string lock_key = std::string(LOCK_PREFIX) + uid_text;
		const std::string identifier = RedisMgr::GetInstance()->AcquireLock(
			lock_key, LOCK_TIME_OUT, ACQUIRE_TIME_OUT);
		if (!identifier.empty())
		{
			Defer release_lock([&identifier, &lock_key]()
			{
				RedisMgr::GetInstance()->ReleaseLock(lock_key, identifier);
			});

			std::string stored_session_id;
			if (RedisMgr::GetInstance()->Get(
				std::string(USER_SESSION_PREFIX) + uid_text,
				stored_session_id) && stored_session_id == uuid_)
			{
				RedisMgr::GetInstance()->Del(
					std::string(USER_SESSION_PREFIX) + uid_text);
				RedisMgr::GetInstance()->Del(
					std::string(USER_IP_PREFIX) + uid_text);
			}
		}
	}

	if (server_ != nullptr)
	{
		server_->ClearSession(uuid_);
	}
}

LogicNode::LogicNode(
	std::shared_ptr<CSession> session,
	std::shared_ptr<RecvNode> receive_node)
	: _session(std::move(session)),
	  _recvnode(std::move(receive_node))
{
}
