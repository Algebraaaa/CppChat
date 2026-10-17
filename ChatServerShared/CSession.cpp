#include "CSession.h"

#include <cstring>
#include <limits>
#include <utility>

#include <json/value.h>

#include "CServer.h"
#include "Logger.h"
#include "LogicSystem.h"
#include "UserMgr.h"

namespace {
const char *IncomingRequestName(short message_id)
{
  switch (message_id) {
  case MSG_CHAT_LOGIN:
    return "chat login request";
  case ID_SEARCH_USER_REQ:
    return "user search request";
  case ID_ADD_FRIEND_REQ:
    return "add-friend request";
  case ID_AUTH_FRIEND_REQ:
    return "friend authorization request";
  case ID_TEXT_CHAT_MSG_REQ:
    return "text chat message request";
  case ID_HEART_BEAT_REQ:
    return "heartbeat request";
  case ID_LOAD_CHAT_THREAD_REQ:
    return "chat-thread list request";
  case ID_CREATE_PRIVATE_CHAT_REQ:
    return "private-chat creation request";
  case ID_LOAD_CHAT_MSG_REQ:
    return "chat-message history request";
  case ID_UPDATE_PROFILE_REQ:
    return "profile update request";
  case ID_GET_AVATAR_REQ:
    return "avatar request";
  case ID_SET_CONTACT_TAGS_REQ:
    return "set-contact-tags request";
  case ID_GROUP_REQ:
    return "group operation request";
  case ID_DELETE_FRIEND_REQ:
    return "delete-friend request";
  default:
    return nullptr;
  }
}
} // namespace

CSession::CSession(boost::asio::io_context &io_context, std::weak_ptr<CServer> server)
    : strand_(boost::asio::make_strand(io_context)), socket_(strand_),
      uuid_(boost::uuids::to_string(boost::uuids::random_generator()())),
      server_(std::move(server)), drain_timer_(strand_), last_heartbeat_(std::time(nullptr)),
      receive_header_node_(std::make_shared<MsgNode>(static_cast<short>(HEAD_TOTAL_LEN)))
{
}

CSession::~CSession() { LOG_DEBUG("TCP session destroyed: uuid=", uuid_, '.'); }

boost::asio::ip::tcp::socket &CSession::GetSocket() { return socket_; }

const std::string &CSession::GetUuid() const { return uuid_; }

const std::string &CSession::GetSessionId() const { return uuid_; }

bool CSession::BindAuthenticatedUser(int uid)
{
  std::lock_guard<std::mutex> lock(login_mutex_);
  if (uid <= 0 || IsClosed() || cleanup_started_ || user_id_ != 0)
    return false;
  user_id_ = uid;
  UserMgr::GetInstance()->SetUserSession(uid, shared_from_this());
  return true;
}

int CSession::GetUserId() const { return user_id_.load(); }

void CSession::Start()
{
  auto self = shared_from_this();
  boost::asio::dispatch(strand_, [self] {
    if (!self->closed_)
      self->AsyncReadHead();
  });
}

void CSession::Send(const std::string &message, short message_id)
{
  if (message.empty() || message.size() > MAX_LENGTH ||
      message.size() > static_cast<std::size_t>(std::numeric_limits<short>::max())) {
    LOG_WARNING("Rejected outbound message: uuid=", uuid_, ", message_id=", message_id,
                ", bytes=", message.size(), '.');
    return;
  }

  Send(message.data(), static_cast<short>(message.size()), message_id);
}

void CSession::Send(const char *message, short message_length, short message_id)
{
  if (message == nullptr || message_length <= 0 ||
      static_cast<std::size_t>(message_length) > MAX_LENGTH) {
    LOG_WARNING("Rejected invalid outbound message: uuid=", uuid_, '.');
    return;
  }

  QueueSend(std::make_shared<SendNode>(message, message_length, message_id), false);
}

bool CSession::QueueSend(std::shared_ptr<SendNode> message, bool close_after_write)
{
  bool start_write = false;
  bool overloaded = false;
  {
    std::lock_guard<std::mutex> lock(send_mutex_);
    if (closed_ || close_after_write_)
      return false;
    overloaded = send_queue_.size() >= MAX_SENDQUE;
    if (!overloaded) {
      start_write = send_queue_.empty();
      send_queue_.push(std::move(message));
      // The final notice and its close marker are admitted atomically.
      close_after_write_ = close_after_write;
      if (close_after_write)
        retiring_ = true;
    }
  }
  if (overloaded) {
    LOG_WARNING("Send queue is full; disconnecting slow consumer: uuid=", uuid_,
                ", limit=", MAX_SENDQUE, '.');
    Close();
    return false;
  }
  if (start_write || close_after_write) {
    auto self = shared_from_this();
    boost::asio::post(strand_, [self, close_after_write] {
      if (close_after_write)
        self->ArmDrainDeadline(std::chrono::seconds(3));
      self->StartWrite();
    });
  }
  return true;
}

void CSession::StartWrite()
{
  if (closed_ || write_active_)
    return;
  std::shared_ptr<SendNode> message_node;
  {
    std::lock_guard<std::mutex> lock(send_mutex_);
    if (send_queue_.empty())
      return;
    message_node = send_queue_.front();
  }
  write_active_ = true;
  auto self = shared_from_this();
  boost::asio::async_write(
      socket_, boost::asio::buffer(message_node->_data, message_node->_total_len),
      boost::asio::bind_executor(
          strand_, [self, message_node](const boost::system::error_code &error, std::size_t) {
            // Keep the buffer alive even if CloseOnExecutor clears the queue.
            self->HandleWrite(error);
          }));
}

void CSession::Close(std::function<void()> on_closed)
{
  const bool already_closed = closed_.exchange(true);
  if (already_closed && !on_closed)
    return;
  auto self = shared_from_this();
  boost::asio::dispatch(strand_, [self, on_closed = std::move(on_closed)] {
    self->CloseOnExecutor();
    if (on_closed)
      on_closed();
  });
}

void CSession::CloseOnExecutor()
{
  drain_timer_.cancel();
  boost::system::error_code ignored_error;
  socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_error);
  socket_.close(ignored_error);
  {
    std::lock_guard<std::mutex> lock(send_mutex_);
    while (!send_queue_.empty())
      send_queue_.pop();
  }
  DealExceptionSession();
  auto handlers = std::move(close_handlers_);
  for (auto &handler : handlers)
    handler();
}

void CSession::ArmDrainDeadline(std::chrono::seconds timeout)
{
  if (closed_)
    return;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  if (drain_timer_active_ && drain_timer_.expiry() <= deadline)
    return; // A shutdown must not extend an existing kick deadline.
  drain_timer_active_ = true;
  drain_timer_.expires_at(deadline);
  auto self = shared_from_this();
  drain_timer_.async_wait(
      boost::asio::bind_executor(strand_, [self](const boost::system::error_code &error) {
        if (!error) {
          LOG_WARNING("Session write drain timed out: uuid=", self->uuid_);
          self->Close();
        }
      }));
}

void CSession::DrainAndClose(std::chrono::seconds timeout, std::function<void()> on_closed)
{
  auto self = shared_from_this();
  boost::asio::dispatch(strand_, [self, timeout, on_closed = std::move(on_closed)] {
    if (self->closed_) {
      self->CloseOnExecutor();
      on_closed();
      return;
    }
    self->close_handlers_.push_back(std::move(on_closed));
    self->retiring_ = true;
    bool empty = false;
    {
      std::lock_guard<std::mutex> lock(self->send_mutex_);
      self->close_after_write_ = true;
      empty = self->send_queue_.empty();
    }
    if (empty) {
      self->Close();
    } else {
      self->ArmDrainDeadline(timeout);
      self->StartWrite();
    }
  });
}

void CSession::AsyncReadBody(std::size_t total_length)
{
  auto self = shared_from_this();
  AsyncReadFull(total_length, [self, total_length](const boost::system::error_code &error,
                                                   std::size_t bytes_transferred) {
    if (error) {
      self->HandleReadFailure("body", error);
      return;
    }
    if (bytes_transferred != total_length) {
      LOG_ERROR("Body length mismatch: uuid=", self->uuid_, ", received=", bytes_transferred,
                ", expected=", total_length, '.');
      self->Close();
      return;
    }

    std::memcpy(self->receive_message_node_->_data, self->data_.data(), bytes_transferred);
    self->receive_message_node_->_cur_len = static_cast<short>(bytes_transferred);
    self->receive_message_node_->_data[total_length] = '\0';

    const short message_id = self->receive_message_node_->GetMessageId();
    const char *request_name = IncomingRequestName(message_id);
    if (message_id == ID_HEART_BEAT_REQ) {
      LOG_INFO("Received heartbeat request: uuid=", self->uuid_, ", message_id=", message_id,
               ", bytes=", bytes_transferred, '.');
    } else if (request_name != nullptr) {
      LOG_DEBUG("Received ", request_name, ": uuid=", self->uuid_, ", message_id=", message_id,
                ", bytes=", bytes_transferred, '.');
    } else {
      LOG_WARNING("Received unrecognized TCP message: uuid=", self->uuid_,
                  ", message_id=", message_id, ", bytes=", bytes_transferred, '.');
    }
    self->UpdateHeartbeat();
    if (auto server = self->server_.lock(); server && server->IsStopping())
      return; // Keep accepted business alive, but do not read another request.
    if (!LogicSystem::GetInstance()->PostMsgToQue(
            std::make_shared<LogicNode>(self, self->receive_message_node_))) {
      if (auto server = self->server_.lock(); server && server->IsStopping())
        return;
      self->Close();
      return;
    }
    self->AsyncReadHead();
  });
}

void CSession::AsyncReadHead()
{
  auto self = shared_from_this();
  AsyncReadFull(HEAD_TOTAL_LEN, [self](const boost::system::error_code &error,
                                       std::size_t bytes_transferred) {
    if (error) {
      self->HandleReadFailure("header", error);
      return;
    }
    if (bytes_transferred != HEAD_TOTAL_LEN) {
      LOG_ERROR("Header length mismatch: uuid=", self->uuid_, '.');
      self->Close();
      return;
    }

    self->receive_header_node_->Clear();
    std::memcpy(self->receive_header_node_->_data, self->data_.data(), HEAD_TOTAL_LEN);

    short message_id = 0;
    std::memcpy(&message_id, self->receive_header_node_->_data, HEAD_ID_LEN);
    message_id = boost::asio::detail::socket_ops::network_to_host_short(message_id);

    short message_length = 0;
    std::memcpy(&message_length, self->receive_header_node_->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
    message_length = boost::asio::detail::socket_ops::network_to_host_short(message_length);

    if (message_id <= 0 || message_length <= 0 ||
        static_cast<std::size_t>(message_length) > MAX_LENGTH) {
      LOG_WARNING("Invalid TCP header: uuid=", self->uuid_, ", message_id=", message_id,
                  ", message_length=", message_length, '.');
      self->Close();
      return;
    }

    self->receive_message_node_ = std::make_shared<RecvNode>(message_length, message_id);
    self->AsyncReadBody(static_cast<std::size_t>(message_length));
  });
}

void CSession::AsyncReadFull(std::size_t total_length, ReadHandler handler)
{
  data_.fill(0);
  AsyncReadLength(0, total_length, std::move(handler));
}

void CSession::AsyncReadLength(std::size_t read_length, std::size_t total_length,
                               ReadHandler handler)
{
  auto self = shared_from_this();
  socket_.async_read_some(
      boost::asio::buffer(data_.data() + read_length, total_length - read_length),
      boost::asio::bind_executor(strand_, [read_length, total_length, handler = std::move(handler),
                                           self](const boost::system::error_code &error,
                                                 std::size_t bytes_transferred) mutable {
        const std::size_t completed_length = read_length + bytes_transferred;
        if (self->closed_) {
          handler(boost::asio::error::operation_aborted, completed_length);
          return;
        }
        if (error || completed_length >= total_length) {
          handler(error, completed_length);
          return;
        }

        self->AsyncReadLength(completed_length, total_length, std::move(handler));
      }));
}

void CSession::HandleWrite(const boost::system::error_code &error)
{
  write_active_ = false;
  if (closed_)
    return;
  if (error) {
    LOG_ERROR("TCP write failed: uuid=", uuid_, ", error=", error.message());
    Close();
    return;
  }

  bool close_after_write = false;
  {
    std::lock_guard<std::mutex> lock(send_mutex_);
    send_queue_.pop();
    close_after_write = send_queue_.empty() && close_after_write_;
  }
  if (close_after_write) {
    Close();
  } else {
    StartWrite();
  }
}

void CSession::HandleReadFailure(const char *stage, const boost::system::error_code &error)
{
  if (error != boost::asio::error::operation_aborted && error != boost::asio::error::eof) {
    LOG_ERROR("TCP ", stage, " read failed: uuid=", uuid_, ", error=", error.message());
  } else {
    LOG_DEBUG("TCP session closed while reading ", stage, ": uuid=", uuid_, '.');
  }
  Close();
}

void CSession::NotifyOffline(int uid)
{
  Json::Value response;
  response["error"] = ErrorCodes::Success;
  response["uid"] = uid;
  const auto message = response.toStyledString();
  QueueSend(std::make_shared<SendNode>(message.data(), static_cast<short>(message.size()),
                                       ID_NOTIFY_OFF_LINE_REQ),
            true);
}

bool CSession::IsHeartbeatExpired(std::time_t now) const
{
  return std::difftime(now, last_heartbeat_.load()) > HEARTBEAT_TIMEOUT_SECONDS;
}

void CSession::UpdateHeartbeat() { last_heartbeat_ = std::time(nullptr); }

void CSession::DealExceptionSession()
{
  int uid = 0;
  {
    std::lock_guard<std::mutex> lock(login_mutex_);
    if (cleanup_started_.exchange(true)) {
      return;
    }
    uid = GetUserId();
  }

  if (auto server = server_.lock())
    server->CleanupSession(uuid_, uid);
}

LogicNode::LogicNode(std::shared_ptr<CSession> session, std::shared_ptr<RecvNode> receive_node)
    : _session(std::move(session)), _recvnode(std::move(receive_node))
{
}
