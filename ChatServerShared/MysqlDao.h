#pragma once
#include "data.h"
#include <cstdint>
#include <json/json.h>
#include <memory>
#include <string>
#include <vector>
class MySqlPool;

/*
 * MysqlDao（Data Access Object，数据访问对象）负责真正执行 SQL。
 * 对外保持原接口；实现按用户、好友和消息领域分组，共用唯一连接池。
 * 跨领域的加好友/初始消息必须继续在同一连接、同一事务中完成。
 */
class MysqlDao {
public:
  MysqlDao();
  ~MysqlDao();

  // name/email/password 来自通过基础校验后的注册请求。
  // 返回值约定：
  //   > 0：注册成功，返回数据库自动生成的用户 uid；
  //   = 0：用户名或邮箱触发唯一索引，说明用户已经存在；
  //   = -1：连接、密码哈希或 SQL 执行出现错误。
  int RegUser(const std::string &name, const std::string &email, const std::string &password);
  bool CheckEmail(const std::string &name, const std::string &email);
  bool UpdatePwd(const std::string &name, const std::string &new_password);
  bool CheckPwdByEmail(const std::string &email, const std::string &password, UserInfo &user_info);
  bool AddFriendApply(int from_uid, int to_uid, const std::string &description,
                      const std::string &remark, const std::string &tags_json,
                      bool *notify_application = nullptr);
  bool AuthFriendApply(int from_uid, int to_uid);
  bool AddFriend(int from_uid, int to_uid, const std::string &remark, const std::string &tags_json,
                 std::vector<std::shared_ptr<ChatMessage>> &initial_messages);
  std::shared_ptr<UserInfo> GetUser(int uid);
  std::shared_ptr<UserInfo> GetUser(const std::string &name);
  int UpdateProfile(int uid, const std::string &name, const std::string &nick,
                    const std::string &description, const std::string &avatar, bool has_avatar);
  bool GetAvatar(int uid, std::string &avatar);
  bool GetApplyList(int to_uid, std::vector<std::shared_ptr<ApplyInfo>> &applications, int begin_id,
                    int limit = 10);
  bool GetFriendList(int self_uid, std::vector<std::shared_ptr<UserInfo>> &friends);
  bool GetFriendPage(int self_uid, std::vector<std::shared_ptr<UserInfo>> &friends, int begin_uid,
                     int limit = 100);
  bool GetFriendIds(int self_uid, std::vector<int> &friend_ids);
  bool DeleteFriend(int self_uid, int friend_uid);
  int GetFriendMetadata(int self_uid, int friend_uid, std::string &back, std::string &tags_json);
  int SetContactTags(int self_uid, int friend_uid, const std::string &tags_json,
                     const std::string *back = nullptr);
  int CheckFriendship(int self_uid, int friend_uid);
  int CheckPrivateChatAccess(int self_uid, int thread_id);
  bool GetUserThreads(std::int64_t user_id, std::int64_t last_id, int page_size,
                      std::vector<std::shared_ptr<ChatThreadInfo>> &threads, bool &load_more,
                      int &next_last_id);
  bool CreatePrivateChat(int user1_uid, int user2_uid, int &thread_id);
  std::shared_ptr<PageResult> LoadChatMsg(int thread_id, int last_message_id, int page_size);
  int GetGroup(int uid, int thread, Json::Value &group, bool members = false);
  int CommonGroups(int uid, const Json::Value &request, Json::Value &result);
  int ChangeGroup(int uid, const Json::Value &request, Json::Value &group);
  bool AddChatMsg(std::vector<std::shared_ptr<ChatMessage>> &messages);

private:
  // MysqlDao 独占连接池；Dao 析构时连接池也会自动析构。
  std::unique_ptr<MySqlPool> _pool;
};
