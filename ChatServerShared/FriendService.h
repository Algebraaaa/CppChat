#pragma once
#include "data.h"
#include <json/json.h>
#include <memory>
#include <string>
#include <vector>

class FriendService {
public:
  static int Apply(int uid, int other, const std::string &description, const std::string &remark,
                   const Json::Value &tags);
  static bool Accept(int uid, int other, const std::string &remark, const Json::Value &tags,
                     std::vector<std::shared_ptr<ChatMessage>> &messages);
  static void NotifyAcceptance(int from, int to, const Json::Value &payload);
  static void NotifyAccepted(int from, int to, Json::Value payload);
  static int Delete(int uid, int other);
  static void NotifyDeleted(int uid, int other);
  static int SetTags(int uid, int other, const Json::Value &input, Json::Value &normalized,
                     const Json::Value &remark = Json::Value());
  static bool List(int uid, std::vector<std::shared_ptr<UserInfo>> &friends, int *error = nullptr);
  static bool Ids(int uid, std::vector<int> &friend_ids);
  static bool Applications(int uid, std::vector<std::shared_ptr<ApplyInfo>> &applications,
                           int *error = nullptr);
  static void WriteApplication(const ApplyInfo &application, Json::Value &item);
};
