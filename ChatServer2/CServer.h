#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <boost/asio.hpp>

#include "CSession.h"

class CServer : public std::enable_shared_from_this<CServer>
{
public:
	CServer(boost::asio::io_context& io_context, unsigned short port);
	~CServer();

	void ClearSession(const std::string& uuid);
	std::shared_ptr<CSession> GetSession(const std::string& uuid);
	bool CheckValid(const std::string& uuid);
	void StartTimer();
	void StopTimer();

private:
	void HandleAccept(
		std::shared_ptr<CSession> session,
		const boost::system::error_code& error);
	void StartAccept();
	void HandleTimer(const boost::system::error_code& error);

	boost::asio::io_context& io_context_;
	unsigned short port_;
	boost::asio::ip::tcp::acceptor acceptor_;
	std::map<std::string, std::shared_ptr<CSession>> sessions_;
	std::mutex mutex_;
	boost::asio::steady_timer heartbeat_timer_;
};
