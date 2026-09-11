#include "CServer.h"

#include <functional>
#include <ctime>
#include <vector>

#include "AsioIOServicePool.h"
#include "ConfigMgr.h"
#include "Logger.h"
#include "RedisMgr.h"
#include "UserMgr.h"
#include "const.h"

CServer::CServer(boost::asio::io_context& io_context, unsigned short port)
	: io_context_(io_context),
	  port_(port),
	  acceptor_(io_context, boost::asio::ip::tcp::endpoint(
		  boost::asio::ip::tcp::v4(), port)),
	  heartbeat_timer_(io_context)
{
	LOG_INFO("TCP acceptor started on port ", port_, '.');
	StartAccept();
}

CServer::~CServer()
{
	boost::system::error_code error;
	try
	{
		heartbeat_timer_.cancel();
	}
	catch (const boost::system::system_error& exception)
	{
		LOG_WARNING("Unable to cancel heartbeat timer: ", exception.what());
	}
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
	int uid = 0;
	bool erased = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto session = sessions_.find(uuid);
		if (session != sessions_.end())
		{
			uid = session->second->GetUserId();
			sessions_.erase(session);
			erased = true;
		}
	}

	if (uid > 0)
	{
		UserMgr::GetInstance()->RmvUserSession(uid, uuid);
	}
	if (erased)
	{
		LOG_INFO("Removed TCP session: uuid=", uuid, '.');
	}
}

std::shared_ptr<CSession> CServer::GetSession(const std::string& uuid)
{
	std::lock_guard<std::mutex> lock(mutex_);
	const auto session = sessions_.find(uuid);
	return session == sessions_.end() ? nullptr : session->second;
}

bool CServer::CheckValid(const std::string& uuid)
{
	std::lock_guard<std::mutex> lock(mutex_);
	return sessions_.find(uuid) != sessions_.end();
}

void CServer::StartTimer()
{
	heartbeat_timer_.expires_after(std::chrono::seconds(HEARTBEAT_SCAN_SECONDS));
	auto self = shared_from_this();
	heartbeat_timer_.async_wait([self](const boost::system::error_code& error)
	{
		self->HandleTimer(error);
	});
}

void CServer::StopTimer()
{
	heartbeat_timer_.cancel();
}

void CServer::HandleTimer(const boost::system::error_code& error)
{
	if (error == boost::asio::error::operation_aborted)
	{
		return;
	}
	if (error)
	{
		LOG_ERROR("Heartbeat timer failed: ", error.message());
		return;
	}

	std::vector<std::shared_ptr<CSession>> sessions;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (const auto& entry : sessions_)
		{
			sessions.push_back(entry.second);
		}
	}

	const std::time_t now = std::time(nullptr);
	std::size_t online_count = 0;
	for (const auto& session : sessions)
	{
		if (session->IsHeartbeatExpired(now))
		{
			LOG_WARNING("Heartbeat expired: uuid=", session->GetUuid(), '.');
			session->Close();
			session->DealExceptionSession();
		}
		else
		{
			++online_count;
		}
	}

	const std::string server_name = ConfigMgr::GetInstance()["SelfServer"]["Name"];
	if (!server_name.empty())
	{
		RedisMgr::GetInstance()->HSet(
			LOGIN_COUNT, server_name, std::to_string(online_count));
	}
	StartTimer();
}

void UserMgr::RmvUserSession(int uid, const std::string& session_id)
{
	std::lock_guard<std::mutex> lock(_session_mtx);
	const auto session = _uid_to_session.find(uid);
	if (session == _uid_to_session.end() ||
		session->second->GetSessionId() != session_id)
	{
		return;
	}
	_uid_to_session.erase(session);
}
