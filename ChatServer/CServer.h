#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <boost/asio.hpp>

#include "CSession.h"

class CServer
{
public:
	CServer(boost::asio::io_context& io_context, unsigned short port);
	~CServer();

	void ClearSession(const std::string& uuid);

private:
	void HandleAccept(
		std::shared_ptr<CSession> session,
		const boost::system::error_code& error);
	void StartAccept();

	boost::asio::io_context& io_context_;
	unsigned short port_;
	boost::asio::ip::tcp::acceptor acceptor_;
	std::map<std::string, std::shared_ptr<CSession>> sessions_;
	std::mutex mutex_;
};
