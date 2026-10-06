#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <json/value.h>

#include "BusinessExecutor.h"
#include "Singleton.h"
#include "data.h"

class CServer;
class CSession;
class LogicNode;

using MessageCallback =
    std::function<void(std::shared_ptr<CSession>, short, const std::string &)>;

class LogicSystem : public Singleton<LogicSystem> {
  friend class Singleton<LogicSystem>;

public:
  ~LogicSystem();
  bool PostMsgToQue(std::shared_ptr<LogicNode> message);
  void SetServer(std::shared_ptr<CServer> server);
  void Stop();
  void BeginStop();
  void ReportMetrics() const;

private:
  LogicSystem();

  void HandleMessage(const std::shared_ptr<LogicNode> &message);
  std::shared_ptr<CServer> GetServer();
  void RegisterCallbacks();
  void LoginHandler(std::shared_ptr<CSession> session, short message_id,
                    const std::string &message_data);
  void SearchInfo(std::shared_ptr<CSession> session, short message_id,
                  const std::string &message_data);
  void AddFriendApply(std::shared_ptr<CSession> session, short message_id,
                      const std::string &message_data);
  void AuthFriendApply(std::shared_ptr<CSession> session, short message_id,
                       const std::string &message_data);
  void DealChatTextMsg(std::shared_ptr<CSession> session, short message_id,
                       const std::string &message_data);
  void HeartBeatHandler(std::shared_ptr<CSession> session, short message_id,
                        const std::string &message_data);
  void GetUserThreadsHandler(std::shared_ptr<CSession> session,
                             short message_id, const std::string &message_data);
  void CreatePrivateChat(std::shared_ptr<CSession> session, short message_id,
                         const std::string &message_data);
  void LoadChatMsg(std::shared_ptr<CSession> session, short message_id,
                   const std::string &message_data);
  void UpdateProfile(std::shared_ptr<CSession> session, short message_id,
                     const std::string &message_data);
  void GetAvatar(std::shared_ptr<CSession> session, short message_id,
                 const std::string &message_data);
  void DeleteFriend(std::shared_ptr<CSession> session, short message_id,
                    const std::string &message_data);
  void SetContactTags(std::shared_ptr<CSession> session, short message_id,
                      const std::string &message_data);

  bool IsPureDigit(const std::string &text) const;
  void GetUserByUid(const std::string &uid_text, Json::Value &response);
  void GetUserByName(const std::string &name, Json::Value &response);

  std::unique_ptr<BusinessExecutor> executor_;
  std::mutex server_mutex_;
  std::map<short, MessageCallback> callbacks_;
  std::shared_ptr<CServer> server_;
};
