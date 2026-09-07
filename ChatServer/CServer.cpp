#include "CServer.h"

#include <functional>

#include "AsioIOServicePool.h"
#include "Logger.h"

CServer::CServer(boost::asio::io_context& io_context, unsigned short port)
	: io_context_(io_context),
	  port_(port),
	  acceptor_(io_context, boost::asio::ip::tcp::endpoint(
		  boost::asio::ip::tcp::v4(), port))
{
	LOG_INFO("TCP acceptor started on port ", port_, '.');
	StartAccept();
}

CServer::~CServer()
{
	boost::system::error_code error;
	acceptor_.close(error);
	LOG_INFO("TCP acceptor stopped on port ", port_, '.');
}

void CServer::HandleAccept(
	std::shared_ptr<CSession> new_session,
	const boost::system::error_code& error)
{
	if (!error)
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			sessions_.emplace(new_session->GetUuid(), new_session);
		}
		LOG_INFO("Accepted TCP session: uuid=", new_session->GetUuid(), '.');
		new_session->Start();
	}
	else if (error != boost::asio::error::operation_aborted)
	{
		LOG_ERROR("TCP accept failed: ", error.message());
	}

	if (acceptor_.is_open())
	{
		StartAccept();
	}
}

void CServer::StartAccept()
{
	auto& io_context = AsioIOServicePool::GetInstance()->GetIOService();
	auto new_session = std::make_shared<CSession>(io_context, this);
	acceptor_.async_accept(
		new_session->GetSocket(),
		std::bind(
			&CServer::HandleAccept,
			this,
			new_session,
			std::placeholders::_1));
}

void CServer::ClearSession(const std::string& uuid)
{
	std::lock_guard<std::mutex> lock(mutex_);
	const std::size_t erased = sessions_.erase(uuid);
	if (erased != 0)
	{
		LOG_INFO("Removed TCP session: uuid=", uuid, '.');
	}
}
