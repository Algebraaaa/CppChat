#include "CServer.h"

#include <ctime>
#include <functional>
#include <future>
#include <vector>

#include "AsioIOServicePool.h"
#include "ConfigMgr.h"
#include "Logger.h"
#include "LogicSystem.h"
#include "RedisMgr.h"
#include "UserMgr.h"
#include "const.h"

CServer::CServer(boost::asio::io_context &io_context, unsigned short port)
    : io_context_(io_context), port_(port),
      acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      heartbeat_timer_(io_context),
      instance_id_(boost::uuids::to_string(boost::uuids::random_generator()()))
{
  try {
    const auto text = ConfigMgr::GetInstance()["Business"].GetValue("MaxConnections", "4096");
    std::size_t consumed = 0;
    const auto limit = std::stoul(text, &consumed);
    if (consumed != text.size() || limit < 1 || limit > 100000)
      throw std::out_of_range("Invalid MaxConnections");
    max_connections_ = limit;
  } catch (const std::exception &) {
    LOG_WARNING("Invalid Business.MaxConnections; using 4096.");
  }
  LOG_INFO("TCP acceptor started on port ", port_, '.');
}

void CServer::Start()
{
  StartAccept();
  StartTimer();
}

void CServer::StopAccepting()
{
  stopping_ = true;
  boost::system::error_code ignored;
  acceptor_.cancel(ignored);
  acceptor_.close(ignored);
  heartbeat_timer_.cancel();
}

CServer::~CServer()
{
  maintenance_.Stop();
  cleanup_.Stop();
  boost::system::error_code error;
  try {
    heartbeat_timer_.cancel();
  } catch (const boost::system::system_error &exception) {
    LOG_WARNING("Unable to cancel heartbeat timer: ", exception.what());
  }
  acceptor_.close(error);
  LOG_INFO("TCP acceptor stopped on port ", port_, '.');
}

void CServer::HandleAccept(std::shared_ptr<CSession> new_session,
                           const boost::system::error_code &error)
{
  if (!error) {
    bool admitted = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!stopping_ && sessions_.size() < max_connections_) {
        sessions_.emplace(new_session->GetUuid(), new_session);
        admitted = true;
      }
    }
    if (admitted) {
      LOG_INFO("Accepted TCP session: uuid=", new_session->GetUuid(), '.');
      new_session->Start();
    } else {
      LOG_WARNING("Chat connection limit reached: limit=", max_connections_);
      new_session->Close();
    }
  } else if (error != boost::asio::error::operation_aborted) {
    LOG_ERROR("TCP accept failed: ", error.message());
  }

  if (acceptor_.is_open()) {
    StartAccept();
  }
}

void CServer::StartAccept()
{
  auto &io_context = AsioIOServicePool::GetInstance()->GetIOService();
  auto new_session = std::make_shared<CSession>(io_context, weak_from_this());
  auto owner = weak_from_this();
  acceptor_.async_accept(new_session->GetSocket(),
                         [owner, new_session](const boost::system::error_code &error) {
                           if (auto self = owner.lock())
                             self->HandleAccept(new_session, error);
                         });
}

void CServer::CleanupSession(const std::string &uuid, int uid)
{
  if (uid > 0)
    UserMgr::GetInstance()->RmvUserSession(uid, uuid);
  if (uid <= 0) {
    ClearSession(uuid);
    return;
  }
  if (!cleanup_.Submit(uuid, [this, uuid, uid] {
        // Lua still compares session IDs; an old cleanup cannot revoke a
        // relogin.
        try {
          if (!RedisMgr::GetInstance()->ReleaseSession(uid, uuid))
            LOG_WARNING("Credential cleanup failed; lease will expire: uid=", uid);
        } catch (const std::exception &error) {
          LOG_WARNING("Credential cleanup exception; lease will expire: uid=", uid,
                      ", error=", error.what());
        } catch (...) {
          LOG_WARNING("Credential cleanup exception; lease will expire: uid=", uid);
        }
        // Even a failed Redis operation must release this closed admission
        // slot.
        ClearSession(uuid);
      })) {
    LOG_WARNING("Credential cleanup rejected; lease will expire: uid=", uid);
    ClearSession(uuid);
  }
}

void CServer::StopCleanup(std::chrono::seconds timeout)
{
  cleanup_.BeginStop();
  const bool drained = cleanup_.WaitIdleUntil(std::chrono::steady_clock::now() + timeout);
  const auto discarded = cleanup_.Stop(drained);
  LOG_INFO("Credential cleanup stopped: drained=", drained, ", deferred_to_lease=", discarded);
  // Active Redis calls have finite command/borrow timeouts and are joined.
  std::lock_guard<std::mutex> lock(mutex_);
  sessions_.clear();
}

void CServer::ClearSession(const std::string &uuid)
{
  int uid = 0;
  bool erased = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto session = sessions_.find(uuid);
    if (session != sessions_.end()) {
      uid = session->second->GetUserId();
      sessions_.erase(session);
      erased = true;
    }
  }

  if (uid > 0) {
    UserMgr::GetInstance()->RmvUserSession(uid, uuid);
  }
  if (erased) {
    LOG_INFO("Removed TCP session: uuid=", uuid, '.');
  }
}

std::shared_ptr<CSession> CServer::GetSession(const std::string &uuid)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto session = sessions_.find(uuid);
  return session == sessions_.end() ? nullptr : session->second;
}

bool CServer::CheckValid(const std::string &uuid)
{
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_.find(uuid) != sessions_.end();
}

void CServer::StartTimer()
{
  heartbeat_timer_.expires_after(std::chrono::seconds(HEARTBEAT_SCAN_SECONDS));
  auto owner = weak_from_this();
  heartbeat_timer_.async_wait([owner](const boost::system::error_code &error) {
    if (auto self = owner.lock())
      self->HandleTimer(error);
  });
}

void CServer::StopTimer()
{
  heartbeat_timer_.cancel();
  maintenance_.Stop();
}

void CServer::HandleTimer(const boost::system::error_code &error)
{
  if (stopping_ || error == boost::asio::error::operation_aborted) {
    return;
  }
  if (error) {
    LOG_ERROR("Heartbeat timer failed: ", error.message());
    return;
  }

  std::vector<std::shared_ptr<CSession>> sessions;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &entry : sessions_) {
      sessions.push_back(entry.second);
    }
  }

  // Redis must not block the acceptor/timer I/O loop. At most one snapshot can
  // wait behind the maintenance task already running.
  if (!maintenance_.Submit("maintenance",
                           [this, sessions = std::move(sessions)] { MaintainSessions(sessions); }))
    LOG_WARNING("Session maintenance is behind; skipping duplicate snapshot.");
  LogicSystem::GetInstance()->ReportMetrics();
  StartTimer();
}

void CServer::MaintainSessions(const std::vector<std::shared_ptr<CSession>> &sessions)
{
  if (stopping_)
    return;
  const std::time_t now = std::time(nullptr);
  std::size_t online_count = 0;
  for (const auto &session : sessions)
    if (!session->IsClosed() && !session->IsHeartbeatExpired(now) && session->GetUserId() > 0)
      ++online_count;
  LOG_INFO("Heartbeat scan: sessions=", sessions.size(), ", online=", online_count, '.');
  if (stopping_)
    return;
  if (!PublishHealth(online_count))
    LOG_WARNING("Unable to renew chat node health lease.");
  for (const auto &session : sessions) {
    if (stopping_)
      break; // Do not renew thousands of leases after admission has stopped.
    if (session->IsClosed())
      continue; // Retiring writes have their own deadline; do not cut them
                // short.
    if (session->IsHeartbeatExpired(now)) {
      LOG_WARNING("Heartbeat expired: uuid=", session->GetUuid(), '.');
      session->Close();
    } else if (session->GetUserId() > 0) {
      const int refreshed =
          RedisMgr::GetInstance()->RefreshSession(session->GetUserId(), session->GetSessionId());
      if (refreshed == 0) {
        session->Close();
        continue;
      }
    }
  }
}

bool CServer::PublishHealth(std::size_t online_count)
{
  const auto name = ConfigMgr::GetInstance()["SelfServer"]["Name"];
  return !name.empty() && RedisMgr::GetInstance()->PublishNode(name, instance_id_, online_count);
}

void CServer::RemoveHealth()
{
  RedisMgr::GetInstance()->RemoveNode(ConfigMgr::GetInstance()["SelfServer"]["Name"], instance_id_);
}

void CServer::CloseSessions(std::chrono::seconds timeout)
{
  std::vector<std::shared_ptr<CSession>> sessions;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &entry : sessions_)
      sessions.push_back(entry.second);
  }
  std::vector<std::future<void>> closed;
  for (const auto &session : sessions) {
    auto completion = std::make_shared<std::promise<void>>();
    closed.push_back(completion->get_future());
    session->DrainAndClose(timeout, [completion] { completion->set_value(); });
  }
  // Never stop the executor before its posted socket closes have executed.
  for (auto &completion : closed)
    completion.get();
}
