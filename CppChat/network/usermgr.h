#ifndef USERMGR_H
#define USERMGR_H

#include "common/singleton.h"
#include "common/userdata.h"
#include <QObject>

// 保存本次登录的用户、好友、申请和会话。界面只读取这里的真实数据。
class UserMgr : public QObject, public Singleton<UserMgr> {
  Q_OBJECT
  friend class Singleton<UserMgr>;
public:
  void Reset();
  void SetUserInfo(std::shared_ptr<UserInfo> user);
  void SetToken(QString token) { _token = token; }
  int GetUid() const;
  QString GetName() const;
  QString GetNick() const;
  QString GetIcon() const;
  QString GetDesc() const;
  std::shared_ptr<UserInfo> GetUserInfo() const { return _user_info; }
  void AppendApplyList(QJsonArray array);
  void AppendFriendList(QJsonArray array);
  const std::vector<std::shared_ptr<ApplyInfo>> &GetApplyList() const { return _apply_list; }
  const std::vector<std::shared_ptr<UserInfo>> &GetFriendList() const { return _friend_list; }
  void AddApplyList(std::shared_ptr<ApplyInfo> apply);
  bool AlreadyApply(int uid) const;
  void MarkApplyAccepted(int uid);
  void RememberOutgoingApply(std::shared_ptr<SearchInfo> user);
  std::shared_ptr<SearchInfo> TakeOutgoingApply(int uid);
  bool CheckFriendById(int uid) const { return _friend_map.contains(uid); }
  // 实时新增的好友放到联系人列表开头，保证分页界面立即可见；
  // 登录时批量恢复好友则传 false，保持服务端返回的原始顺序。
  void AddFriend(std::shared_ptr<UserInfo> user, bool addToFront = true);
  void AddFriend(std::shared_ptr<AuthRsp> auth);
  void AddFriend(std::shared_ptr<AuthInfo> auth);
  std::shared_ptr<UserInfo> GetFriendById(int uid) const;
  void AddChatThreadData(std::shared_ptr<ChatThreadData> data, int otherUid);
  std::shared_ptr<ChatThreadData> GetChatThreadByThreadId(int threadId) const;
  std::shared_ptr<ChatThreadData> GetChatThreadByUid(int uid) const;
  const QMap<int, std::shared_ptr<ChatThreadData>> &GetChatThreads() const { return _chat_map; }
private:
  UserMgr() = default;
  std::shared_ptr<UserInfo> _user_info;
  QString _token;
  std::vector<std::shared_ptr<ApplyInfo>> _apply_list;
  // 发起好友申请时已经查询到的对方资料，用于校正好友通过通知中的资料。
  QMap<int, std::shared_ptr<SearchInfo>> _outgoing_apply_map;
  std::vector<std::shared_ptr<UserInfo>> _friend_list;
  QMap<int, std::shared_ptr<UserInfo>> _friend_map;
  QMap<int, std::shared_ptr<ChatThreadData>> _chat_map;
  QMap<int, int> _uid_to_thread_id;
};
#endif // USERMGR_H
