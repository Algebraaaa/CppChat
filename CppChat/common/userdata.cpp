#include "userdata.h"
SearchInfo::SearchInfo(int uid, QString name,
    QString nick, QString desc, int sex, QString icon):_uid(uid)
  ,_name(name), _nick(nick),_desc(desc),_sex(sex),_icon(icon){
}

AddFriendApply::AddFriendApply(int from_uid, QString name, QString desc,
                               QString icon, QString nick, int sex)
    :_from_uid(from_uid),_name(name),
      _desc(desc),_icon(icon),_nick(nick),_sex(sex)
{

}

ChatDataBase::ChatDataBase(int msg_id, int thread_id, ChatFormType form_type,
    ChatMsgType msg_type, QString content, int send_uid, int status, QString chat_time):_msg_id(msg_id),
_thread_id(thread_id), _form_type(form_type),
_msg_type(msg_type), _content(content), _send_uid(send_uid), _status(status), _chat_time(chat_time){

}

ChatDataBase::ChatDataBase(QString unique_id, int thread_id, ChatFormType form_type,
    ChatMsgType msg_type, QString content, int send_uid, int status, QString chat_time):_unique_id(unique_id),
    _thread_id(thread_id), _form_type(form_type),
    _msg_type(msg_type), _content(content), _send_uid(send_uid),_msg_id(0), _status(status), _chat_time(chat_time)
{

}

ChatDataBase::ChatDataBase(int msg_id, QString unique_id, int thread_id, ChatFormType form_type, ChatMsgType msg_type,
    QString content, int send_uid, int status, QString chat_time):_msg_id(msg_id), _unique_id(unique_id),
    _thread_id(thread_id), _form_type(form_type),
    _msg_type(msg_type), _content(content), _send_uid(send_uid), _status(status), _chat_time(chat_time) {

}

void ChatDataBase::SetUniqueId(const QString &unique_id)
{
    _unique_id = unique_id;
}

QString ChatDataBase::GetUniqueId()
{
    return _unique_id;
}

void ChatThreadData::AddMsg(std::shared_ptr<ChatDataBase> msg)
{
    _msg_map.insert(msg->GetMsgId(), msg);
    if (msg->GetMsgId() >= _last_msg_id) {
        _last_msg = msg->GetMsgContent();
        _last_msg_id = msg->GetMsgId();
    }
}

void ChatThreadData::MoveMsg(std::shared_ptr<ChatDataBase> msg) {

    auto iter = _msg_unrsp_map.find(msg->GetUniqueId());
    if (iter == _msg_unrsp_map.end()) {
        AddMsg(msg);
        return;
    }

    iter.value()->SetMsgId(msg->GetMsgId());
    iter.value()->SetStatus(msg->GetStatus());
    AddMsg(iter.value());
    _msg_unrsp_map.erase(iter);
    _pendingOrder.removeAll(msg->GetUniqueId());
}

void ChatThreadData::SetLastMsgId(int msg_id)
{
    _last_msg_id = msg_id;
}

void ChatThreadData::SetOtherId(int other_id)
{
    _other_id = other_id;
}

int  ChatThreadData::GetOtherId() {
    return _other_id;
}

QString ChatThreadData::GetGroupName()
{
    return _group_name;
}

QMap<int, std::shared_ptr<ChatDataBase>> ChatThreadData::GetMsgMap() {
    return _msg_map;
}

int ChatThreadData::GetThreadId()
{
    return _thread_id;
}

QMap<int, std::shared_ptr<ChatDataBase>>& ChatThreadData::GetMsgMapRef()
{
    return _msg_map;
}


void ChatThreadData::AppendMsg(int msg_id, std::shared_ptr<ChatDataBase> base_msg) {
    _msg_map.insert(msg_id, base_msg);
    if (msg_id >= _last_msg_id) {
        _last_msg = base_msg->GetMsgContent();
        _last_msg_id = msg_id;
    }
}

QString ChatThreadData::GetLastMsg()
{
    if (!_pendingOrder.isEmpty()) {
        return _msg_unrsp_map.value(_pendingOrder.last())->GetContent();
    }
    return _last_msg;
}

int ChatThreadData::GetLastMsgId()
{
    return _last_msg_id;
}

void ChatThreadData::AppendUnRspMsg(QString unique_id, std::shared_ptr<ChatDataBase> base_msg)
{
    if (!_msg_unrsp_map.contains(unique_id)) _pendingOrder.append(unique_id);
    _msg_unrsp_map.insert(unique_id, base_msg);
    _last_msg = base_msg->GetContent();
}

std::vector<std::shared_ptr<ChatDataBase>> ChatThreadData::GetPendingMessages() const
{
    std::vector<std::shared_ptr<ChatDataBase>> messages;
    for (const auto &id : _pendingOrder) messages.push_back(_msg_unrsp_map.value(id));
    return messages;
}


QMap<QString, std::shared_ptr<ChatDataBase>>& ChatThreadData::GetMsgUnRspRef() {
    return _msg_unrsp_map;
}

void AuthInfo::SetChatDatas(std::vector<std::shared_ptr<TextChatData>> chat_datas)
{
    _chat_datas = chat_datas;
    _thread_id = _chat_datas.empty() ? 0 : _chat_datas.front()->GetThreadId();
}

void AuthRsp::SetChatDatas(std::vector<std::shared_ptr<TextChatData>> chat_datas) {
    _chat_datas = chat_datas;
    _thread_id = _chat_datas.empty() ? 0 : _chat_datas.front()->GetThreadId();
}
