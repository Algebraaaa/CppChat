#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <ctime>

#include <boost/asio.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "MsgNode.h"
#include "const.h"

class CServer;
class LogicSystem;

class CSession : public std::enable_shared_from_this<CSession>
{
public:
	CSession(boost::asio::io_context& io_context, CServer* server);
	~CSession();

	boost::asio::ip::tcp::socket& GetSocket();
	const std::string& GetUuid() const;
	const std::string& GetSessionId() const;
	void SetUserId(int uid);
	int GetUserId() const;

	void Start();
	void Send(const char* message, short message_length, short message_id);
	void Send(const std::string& message, short message_id);
	void Close();
	void NotifyOffline(int uid);
	bool IsHeartbeatExpired(std::time_t now) const;
	void UpdateHeartbeat();
	void DealExceptionSession();

private:
	using ReadHandler = std::function<void(
		const boost::system::error_code&,
		std::size_t)>;

	void AsyncReadBody(std::size_t total_length);
	void AsyncReadHead();
	void AsyncReadFull(std::size_t total_length, ReadHandler handler);
	void AsyncReadLength(
		std::size_t read_length,
		std::size_t total_length,
		ReadHandler handler);
	void HandleWrite(const boost::system::error_code& error);
	void HandleReadFailure(
		const char* stage,
		const boost::system::error_code& error);

	boost::asio::ip::tcp::socket socket_;
	std::string uuid_;
	std::array<char, MAX_LENGTH> data_{};
	CServer* server_;
	std::atomic_bool closed_{ false };
	std::atomic_bool cleanup_started_{ false };
	std::atomic_int user_id_{ 0 };
	std::atomic<std::time_t> last_heartbeat_;
	std::queue<std::shared_ptr<SendNode>> send_queue_;
	std::mutex send_mutex_;
	std::shared_ptr<RecvNode> receive_message_node_;
	std::shared_ptr<MsgNode> receive_header_node_;
};

class LogicNode
{
	friend class LogicSystem;

public:
	LogicNode(
		std::shared_ptr<CSession> session,
		std::shared_ptr<RecvNode> receive_node);

private:
	std::shared_ptr<CSession> _session;
	std::shared_ptr<RecvNode> _recvnode;
};
