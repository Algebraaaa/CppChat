#ifndef USERDATA_H
#define USERDATA_H
#include "global.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

class SearchInfo {
public:
  SearchInfo(int uid, QString name, QString nick, QString desc, int sex, QString icon);
  int _uid;
  QString _name;
  QString _nick;
  QString _desc;
  int _sex;
  QString _icon;
};

class AddFriendApply {
public:
  AddFriendApply(int from_uid, QString name, QString desc, QString icon, QString nick, int sex);
  int _from_uid;
  QString _name;
  QString _desc;
  QString _icon;
  QString _nick;
  int _sex;
};

struct ApplyInfo {
  ApplyInfo(int uid, QString name, QString desc, QString icon, QString nick, int sex, int status)
      : _uid(uid), _name(name), _desc(desc), _icon(icon), _nick(nick), _sex(sex), _status(status)
  {
  }

  ApplyInfo(std::shared_ptr<AddFriendApply> addinfo)
      : _uid(addinfo->_from_uid), _name(addinfo->_name), _desc(addinfo->_desc),
        _icon(addinfo->_icon), _nick(addinfo->_nick), _sex(addinfo->_sex), _status(0)
  {
  }
  void SetIcon(QString head) { _icon = head; }
  int _uid;
  QString _name;
  QString _desc;
  QString _icon;
  QString _nick;
  int _sex;
  int _status;
};

class TextChatData;
struct AuthInfo {
  AuthInfo(int uid, QString name, QString nick, QString icon, int sex)
      : _uid(uid), _name(name), _nick(nick), _icon(icon), _sex(sex), _thread_id(0)
  {
  }

  void SetChatDatas(std::vector<std::shared_ptr<TextChatData>> _chat_datas);
  int _uid;
  QString _name;
  QString _nick;
  QString _icon;
  int _sex;
  int _thread_id;
  std::vector<std::shared_ptr<TextChatData>> _chat_datas;
};

struct AuthRsp {
  AuthRsp(int peer_uid, QString peer_name, QString peer_nick, QString peer_icon, int peer_sex)
      : _uid(peer_uid), _name(peer_name), _nick(peer_nick), _icon(peer_icon), _sex(peer_sex),
        _thread_id(0)
  {
  }

  void SetChatDatas(std::vector<std::shared_ptr<TextChatData>> _chat_datas);
  int _uid;
  QString _name;
  QString _nick;
  QString _icon;
  int _sex;
  int _thread_id;
  std::vector<std::shared_ptr<TextChatData>> _chat_datas;
};

struct UserInfo {
  UserInfo(int uid, QString name, QString nick, QString icon, int sex, QString = "",
           QString desc = "")
      : _uid(uid), _name(name), _nick(nick), _icon(icon), _sex(sex), _desc(desc)
  {
  }

  UserInfo(std::shared_ptr<AuthInfo> auth)
      : _uid(auth->_uid), _name(auth->_name), _nick(auth->_nick), _icon(auth->_icon),
        _sex(auth->_sex), _desc("")
  {
  }

  UserInfo(int uid, QString name, QString icon)
      : _uid(uid), _name(name), _nick(_name), _icon(icon), _sex(0), _desc("")
  {
  }

  UserInfo(std::shared_ptr<AuthRsp> auth)
      : _uid(auth->_uid), _name(auth->_name), _nick(auth->_nick), _icon(auth->_icon),
        _sex(auth->_sex), _desc("")
  {
  }

  UserInfo(std::shared_ptr<SearchInfo> search_info)
      : _uid(search_info->_uid), _name(search_info->_name), _nick(search_info->_nick),
        _icon(search_info->_icon), _sex(search_info->_sex), _desc(search_info->_desc)
  {
  }

  int _uid;
  QString _name;
  QString _nick;
  QString _icon;
  int _sex;
  QString _desc;
  QString _back;
  QStringList _tags; // 当前账号为这位联系人设置的标签。
};

class ChatDataBase {
public:
  ChatDataBase(int msg_id, int thread_id, ChatFormType form_type, ChatMsgType msg_type,
               QString content, int _send_uid, int status, QString chat_time);
  ChatDataBase(QString unique_id, int thread_id, ChatFormType form_type, ChatMsgType msg_type,
               QString content, int send_uid, int status, QString chat_time);
  ChatDataBase(int msg_id, QString unique_id, int thread_id, ChatFormType form_type,
               ChatMsgType msg_type, QString content, int send_uid, int status, QString chat_time);
  int GetMsgId() { return _msg_id; }
  int GetThreadId() { return _thread_id; }
  ChatFormType GetFormType() { return _form_type; }
  ChatMsgType GetMsgType() { return _msg_type; }
  QString GetContent() { return _content; }
  void SetContent(const QString &content) { _content = content; }
  int GetSendUid() { return _send_uid; }
  QString GetMsgContent() { return _content; }
  QString GetChatTime() const { return _chat_time; }
  void SetChatTime(const QString &chat_time) { _chat_time = chat_time; }
  void SetUniqueId(const QString &unique_id);
  QString GetUniqueId();
  virtual ~ChatDataBase() = default;
  int GetStatus() { return _status; }
  void SetMsgId(int msg_id) { _msg_id = msg_id; }
  void SetStatus(int status) { _status = status; }

private:
  QString _unique_id;
  int _msg_id;
  int _thread_id;
  ChatFormType _form_type;
  ChatMsgType _msg_type;
  QString _content;
  int _send_uid;
  // 服务端返回的消息状态。
  int _status;
  QString _chat_time;
};

class TextChatData : public ChatDataBase {
public:
  TextChatData(int msg_id, int thread_id, ChatFormType form_type, ChatMsgType msg_type,
               QString content, int send_uid, int status, QString chat_time = "")
      : ChatDataBase(msg_id, thread_id, form_type, msg_type, content, send_uid, status, chat_time)
  {
  }

  TextChatData(QString unique_id, int thread_id, ChatFormType form_type, ChatMsgType msg_type,
               QString content, int send_uid, int status, QString chat_time = "")
      : ChatDataBase(unique_id, thread_id, form_type, msg_type, content, send_uid, status,
                     chat_time)
  {
  }

  TextChatData(int msg_id, QString unique_id, int thread_id, ChatFormType form_type,
               ChatMsgType msg_type, QString content, int send_uid, int status,
               QString chat_time = "")
      : ChatDataBase(msg_id, unique_id, thread_id, form_type, msg_type, content, send_uid, status,
                     chat_time)
  {
  }
};

struct ChatThreadInfo {
  QJsonObject _group;
  int _thread_id = 0;
  QString _type; // "private" or "group"
  int _user1_id = 0;
  int _user2_id = 0;
  int _last_message_id = 0;
  QString _last_chat_time;
  bool _has_last_chat_summary = false;
};

class ChatThreadData {
public:
  ChatThreadData(int other_id, int thread_id, int last_msg_id)
      : _other_id(other_id), _last_msg_id(last_msg_id), _thread_id(thread_id)
  {
  }
  void AddMsg(std::shared_ptr<ChatDataBase> msg);
  void ClearLocalMessages()
  {
    _msg_map.clear();
    _last_msg.clear();
  }
  void MoveMsg(std::shared_ptr<ChatDataBase> msg);
  void SetLastMsgId(int msg_id);
  void SetLastChatSummary(int msg_id, const QString &chat_time);
  void RemoveLocalMessage(int msg_id);
  void ForgetPendingMessage(int senderUid, const QString &uniqueId);
  void RestoreLocalSummary(int msg_id, const QString &summary);
  void SetOtherId(int other_id);
  int GetOtherId();
  QString GetGroupName();
  bool IsGroup() const { return _other_id == 0; }
  void SetGroup(const QJsonObject &group)
  {
    auto names = _group["member_names"].toObject();
    for (const auto &value : group["members"].toArray()) {
      const auto member = value.toObject();
      names[QString::number(member["uid"].toInt())] = member["name"];
    }
    const auto incoming = group["member_names"].toObject();
    for (auto it = incoming.begin(); it != incoming.end(); ++it)
      names[it.key()] = it.value();
    for (auto it = group.begin(); it != group.end(); ++it)
      _group[it.key()] = it.value();
    _group["member_names"] = names;
    _group_name = _group["name"].toString();
  }
  QJsonObject Group() const { return _group; }
  QString MemberName(int uid) const
  {
    const auto name = _group["member_names"].toObject()[QString::number(uid)].toString();
    return name.isEmpty() ? QStringLiteral("用户 %1").arg(uid) : name;
  }
  QMap<int, std::shared_ptr<ChatDataBase>> GetMsgMap();
  int GetThreadId();
  QMap<int, std::shared_ptr<ChatDataBase>> &GetMsgMapRef();
  void AppendMsg(int msg_id, std::shared_ptr<ChatDataBase> base_msg);
  QString GetLastMsg();
  int GetLastMsgId();
  QString GetLastChatTime() const;
  QMap<QString, std::shared_ptr<ChatDataBase>> &GetMsgUnRspRef();
  void AppendUnRspMsg(QString unique_id, std::shared_ptr<ChatDataBase> base_msg);
  std::vector<std::shared_ptr<ChatDataBase>> GetPendingMessages() const;

private:
  int _other_id;
  int _last_msg_id;
  int _thread_id;
  QString _last_msg;
  QString _last_chat_time;
  QJsonObject _group;
  QString _group_name;
  QMap<int, std::shared_ptr<ChatDataBase>> _msg_map;
  QMap<QString, std::shared_ptr<ChatDataBase>> _msg_unrsp_map;
  QList<QString> _pendingOrder;
};

#endif
