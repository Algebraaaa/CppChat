#pragma once
#include "data.h"
#include <json/json.h>
#include <memory>
#include <string>

struct ProfileUpdateResult {
  int error = 0;
  bool committed = false;
};

// Stateless domain service. Protocol adapters provide authenticated user IDs.
class UserService {
public:
  static bool GetById(int uid, std::shared_ptr<UserInfo> &user);
  static std::shared_ptr<UserInfo> GetByName(const std::string &name);
  static void WriteProfile(const UserInfo &user, Json::Value &value);
  static ProfileUpdateResult UpdateProfile(int uid, const std::string &name,
                                           const std::string &nick,
                                           const std::string &description,
                                           const std::string &avatar,
                                           bool has_avatar);
  static bool GetAvatar(int uid, std::string &avatar);
};
