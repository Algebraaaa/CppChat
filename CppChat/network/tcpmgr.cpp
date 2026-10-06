#include "tcpmgr.h"
#include <QScopedValueRollback>
#include "usermgr.h"
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QVector>
#include <QUuid>

namespace {
constexpr int kHeartbeatIntervalMs = 10000;
constexpr int kHeartbeatReplyTimeoutMs = 30000;

QString timeoutMessage(ReqId request)
{
  switch (request) {
  case ID_LOAD_CHAT_THREAD_REQ:
    return QObject::tr("会话列表加载超时，请重试");
  case ID_LOAD_CHAT_MSG_REQ:
    return QObject::tr("历史聊天记录加载超时，请重试");
  case ID_CREATE_PRIVATE_CHAT_REQ:
    return QObject::tr("创建聊天会话超时，请重试");
  case ID_SEARCH_USER_REQ:
    return QObject::tr("搜索用户超时，请重试");
  case ID_ADD_FRIEND_REQ:
    return QObject::tr("发送好友申请超时，请重试");
  case ID_AUTH_FRIEND_REQ:
    return QObject::tr("处理好友申请超时，请重试");
  case ID_SET_CONTACT_TAGS_REQ:
    return QObject::tr("保存标签超时，请重试");
  case ID_DELETE_FRIEND_REQ:
    return QObject::tr("删除好友超时，请重试");
  case ID_CHAT_LOGIN:
    return QObject::tr("聊天服务器登录超时，请重试");
  case ID_UPDATE_PROFILE_REQ:
    return QObject::tr("保存资料超时，请重试");
  default:
    return QObject::tr("服务器响应超时，请重试");
  }
}

// 回包编号与请求编号对应，通知包不属于任何客户端请求。
ReqId requestForResponse(ReqId response)
{
  switch (response) {
  case ID_CHAT_LOGIN_RSP:
    return ID_CHAT_LOGIN;
  case ID_SEARCH_USER_RSP:
    return ID_SEARCH_USER_REQ;
  case ID_ADD_FRIEND_RSP:
    return ID_ADD_FRIEND_REQ;
  case ID_AUTH_FRIEND_RSP:
    return ID_AUTH_FRIEND_REQ;
  case ID_SET_CONTACT_TAGS_RSP:
    return ID_SET_CONTACT_TAGS_REQ;
  case ID_DELETE_FRIEND_RSP:
    return ID_DELETE_FRIEND_REQ;
  case ID_TEXT_CHAT_MSG_RSP:
    return ID_TEXT_CHAT_MSG_REQ;
  case ID_LOAD_CHAT_THREAD_RSP:
    return ID_LOAD_CHAT_THREAD_REQ;
  case ID_CREATE_PRIVATE_CHAT_RSP:
    return ID_CREATE_PRIVATE_CHAT_REQ;
  case ID_LOAD_CHAT_MSG_RSP:
    return ID_LOAD_CHAT_MSG_REQ;
  case ID_UPDATE_PROFILE_RSP:
    return ID_UPDATE_PROFILE_REQ;
  default:
    return static_cast<ReqId>(0);
  }
}

// 历史/认证回包和实时消息回包采用不同字段名，集中在此转换。
std::vector<std::shared_ptr<TextChatData>> readMessages(const QJsonObject &object)
{
  std::vector<std::shared_ptr<TextChatData>> messages;
  for (const auto &value : object["chat_datas"].toArray()) {
    const auto msg = value.toObject();
    const int id = msg.value("message_id").toInt(msg["msg_id"].toInt());
    const int thread = msg.value("thread_id").toInt(object["thread_id"].toInt());
    const int sender = msg.value("sender").toInt(object["fromuid"].toInt());
    const QString content = msg.value("content").toString(msg["msg_content"].toString());
    if (id <= 0 || thread <= 0 || sender <= 0)
      continue;
    messages.push_back(std::make_shared<TextChatData>(
        id, msg["unique_id"].toString(), thread, ChatFormType::PRIVATE,
        msg.value("msg_type").toInt() == 1 ? ChatMsgType::PIC : ChatMsgType::TEXT, content, sender,
        msg["status"].toInt(), msg["chat_time"].toString()));
  }
  return messages;
}
} // namespace

TcpMgr::TcpMgr()
{
  _socket.setReadBufferSize(1024 * 1024);
  _connectTimer.setSingleShot(true);
  _connectTimer.setInterval(15000);
  _heartbeatTimer.setInterval(kHeartbeatIntervalMs);
  connect(&_connectTimer, &QTimer::timeout, this, [this] {
    CloseConnection();
    emit sig_con_success(false);
  });
  connect(&_socket, &QTcpSocket::connected, this, [this] {
    _connectTimer.stop();
    emit sig_con_success(true);
  });
  connect(&_socket, &QTcpSocket::readyRead, this, &TcpMgr::readPackets);
  connect(&_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
    qWarning() << "TCP error:" << error << _socket.errorString();
    if (!_loggedIn) {
      _connectTimer.stop();
      stopRequestTimer(ID_CHAT_LOGIN);
      emit sig_con_success(false);
    }
  });
  connect(&_socket, &QTcpSocket::disconnected, this, [this] {
    const bool wasLoggedIn = _loggedIn;
    resetConnectionState();
    if (wasLoggedIn)
      emit sig_connection_closed();
  });
  connect(&_heartbeatTimer, &QTimer::timeout, this, [this] {
    if (!_loggedIn)
      return;
    if (_lastHeartbeat.elapsed() >= kHeartbeatReplyTimeoutMs) {
      CloseConnection();
      emit sig_connection_closed();
      return;
    }
    _heartbeatFriends.clear();
    for (const auto &user : UserMgr::GetInstance()->GetFriendList())
      _heartbeatFriends.insert(user->_uid, user);
    const QJsonObject request{{"fromuid", UserMgr::GetInstance()->GetUid()},
                              {"friend_snapshot_id", ++_heartbeatSnapshotId}};
    slot_send_data(ID_HEART_BEAT_REQ, QJsonDocument(request).toJson(QJsonDocument::Compact));
  });
  connect(this, &TcpMgr::sig_send_data, this, &TcpMgr::slot_send_data);
  initHandlers();
}

bool TcpMgr::IsConnected() const
{
  return _loggedIn && _socket.state() == QAbstractSocket::ConnectedState;
}

void TcpMgr::resetConnectionState()
{
  ++_connectionGeneration;
  _readScheduled = false;
  _loggedIn = false;
  _messageDedup = false;
  _requestCorrelation = false;
  _connectTimer.stop();
  _heartbeatTimer.stop();
  for (auto *timer : _requestTimers)
    timer->stop();
  _pendingRequests.clear();
  _heartbeatSnapshotId = 0;
  _heartbeatFriends.clear();
  _buffer.clear();
}

void TcpMgr::CloseConnection()
{
  resetConnectionState();
  _socket.abort();
}

void TcpMgr::slot_tcp_connect(ServerInfo server)
{
  CloseConnection();
  bool valid = false;
  const uint port = server.Port.toUInt(&valid);
  if (!valid || port == 0 || port > 65535 || server.Host.trimmed().isEmpty()) {
    emit sig_con_success(false);
    return;
  }
  _connectTimer.start();
  _socket.connectToHost(server.Host, static_cast<quint16>(port));
}

void TcpMgr::readPackets()
{
  if (_readingPackets) {
    // 嵌套事件循环不能重入解析；外层结束时会检查剩余数据并继续调度。
    return;
  }
  QScopedValueRollback<bool> reading(_readingPackets, true);
  const auto generation = _connectionGeneration;
  const qint64 room = qMax<qint64>(0, 1024 * 1024 - _buffer.size());
  _buffer.append(_socket.read(qMin<qint64>(room, 256 * 1024)));
  QElapsedTimer budget;
  budget.start();
  qsizetype consumed = 0;
  int packets = 0;
  while (_buffer.size() - consumed >= 4 && packets < 32 && budget.elapsed() < 4) {
    const quint16 id = (quint8(_buffer[consumed]) << 8) | quint8(_buffer[consumed + 1]);
    const quint16 length = (quint8(_buffer[consumed + 2]) << 8) | quint8(_buffer[consumed + 3]);
    if (_buffer.size() - consumed < 4 + length)
      break;
    const auto body = _buffer.mid(consumed + 4, length);
    consumed += 4 + length;
    ++packets;
    handleMsg(static_cast<ReqId>(id), body);
    if (_connectionGeneration != generation) {
      if (_socket.bytesAvailable() > 0 || !_buffer.isEmpty())
        schedulePacketRead();
      return;
    }
  }
  // 一批只移动一次剩余字节，避免每包 remove(0, ...) 反复搬运整个积压缓冲。
  if (consumed)
    _buffer.remove(0, consumed);
  bool complete = false;
  if (_buffer.size() >= 4) {
    const int length = (quint8(_buffer[2]) << 8) | quint8(_buffer[3]);
    complete = _buffer.size() >= 4 + length;
  }
  if (complete || _socket.bytesAvailable() > 0)
    schedulePacketRead();
}

void TcpMgr::schedulePacketRead()
{
  if (_readScheduled)
    return;
  _readScheduled = true;
  const auto generation = _connectionGeneration;
  QTimer::singleShot(0, this, [this, generation] {
    if (_connectionGeneration != generation)
      return;
    _readScheduled = false;
    readPackets();
  });
}

void TcpMgr::startRequestTimer(ReqId request)
{
  // 文本可以连续发送，逐条超时由 MessageSender 按 unique_id 管理。
  if (request == ID_TEXT_CHAT_MSG_REQ || request == ID_HEART_BEAT_REQ ||
      request == ID_GET_AVATAR_REQ)
    return;
  if (!_requestTimers.contains(request)) {
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(15000);
    _requestTimers.insert(request, timer);
    connect(timer, &QTimer::timeout, this,
            [this, request] { failRequest(request, timeoutMessage(request)); });
  }
  _requestTimers.value(request)->start();
}
void TcpMgr::stopRequestTimer(ReqId request)
{
  if (_requestTimers.contains(request))
    _requestTimers.value(request)->stop();
  _pendingRequests.remove(request);
}
void TcpMgr::CancelRequest(ReqId request)
{
  stopRequestTimer(request);
}
void TcpMgr::failRequest(ReqId request, const QString &message)
{
  stopRequestTimer(request);
  if (request == ID_CHAT_LOGIN) {
    CloseConnection();
    emit sig_login_failed(NetworkError);
  }
  emit sig_request_failed(request, message);
}

void TcpMgr::slot_send_data(ReqId id, QByteArray body)
{
  const bool tracked =
      id != ID_TEXT_CHAT_MSG_REQ && id != ID_HEART_BEAT_REQ && id != ID_GET_AVATAR_REQ;
  QJsonObject request;
  if (tracked) {
    const auto document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
      emit sig_send_failed(id);
      failRequest(id, tr("请求 JSON 无效"));
      return;
    }
    request = document.object();
    // Every attempt has a new transport ID; message UUIDs remain unchanged.
    request["request_id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    body = QJsonDocument(request).toJson(QJsonDocument::Compact);
  }
  const int maxBody = id == ID_UPDATE_PROFILE_REQ ? 16000 : 2048;
  if (body.size() > maxBody || _socket.state() != QAbstractSocket::ConnectedState) {
    emit sig_send_failed(id);
    failRequest(id, tr("消息过大或聊天连接已断开"));
    return;
  }
  QByteArray packet;
  QDataStream stream(&packet, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::BigEndian);
  stream << static_cast<quint16>(id) << static_cast<quint16>(body.size());
  packet.append(body);
  if (id == ID_ADD_FRIEND_REQ)
    _applyTarget = QJsonDocument::fromJson(body).object()["touid"].toInt();
  if (tracked)
    _pendingRequests.insert(id, request);
  startRequestTimer(id);
  qInfo() << "TCP request queued:" << static_cast<int>(id) << "bytes:" << body.size();
  if (id == ID_TEXT_CHAT_MSG_REQ) {
    const auto chat = QJsonDocument::fromJson(body).object();
    for (const auto &value : chat["text_array"].toArray()) {
      const auto message = value.toObject();
      const bool image = message["msg_type"].toInt() == 1;
      qInfo() << "Chat message queued:" << "type=" << (image ? "image" : "text")
              << "thread=" << chat["thread_id"].toInt() << "recipient=" << chat["touid"].toInt();
    }
  }
  if (_socket.write(packet) != packet.size()) {
    emit sig_send_failed(id);
    failRequest(id, tr("消息发送失败，请重试"));
  }
}

void TcpMgr::handleMsg(ReqId id, const QByteArray &data)
{
  qInfo() << "TCP response received:" << static_cast<int>(id) << "bytes:" << data.size();
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(data, &parseError);
  const auto request = requestForResponse(id);
  const auto object = document.object();
  const int error = object.value("error").toInt(Error_Json);
  if (request != 0 && request != ID_TEXT_CHAT_MSG_REQ) {
    // 已超时的回包不再驱动界面；有目标字段时，先核对它是否属于当前请求。
    if (!_pendingRequests.contains(request))
      return;
    const auto pending = _pendingRequests.value(request);
    if (object.contains("request_id")) {
      if (!object["request_id"].isString() ||
          object["request_id"].toString() != pending["request_id"].toString())
        return;
    } else if (_requestCorrelation || request == ID_SET_CONTACT_TAGS_REQ) {
      // Upgraded servers must echo it, including errors; old servers keep the
      // existing target-field fallback (which cannot disambiguate every retry).
      return;
    }
    if (request == ID_LOAD_CHAT_MSG_REQ && object.contains("thread_id") &&
        object["thread_id"].toInt() != pending["thread_id"].toInt())
      return;
    if (request == ID_CREATE_PRIVATE_CHAT_REQ && object.contains("other_id") &&
        object["other_id"].toInt() != pending["other_id"].toInt())
      return;
    if ((request == ID_DELETE_FRIEND_REQ || request == ID_SET_CONTACT_TAGS_REQ) &&
        object.contains("friend_uid") &&
        object["friend_uid"].toInt() != pending["friend_uid"].toInt())
      return;
    stopRequestTimer(request);
  }
  if (parseError.error != QJsonParseError::NoError || !document.isObject() || error != Success) {
    if (id == ID_HEARTBEAT_RSP && _loggedIn) {
      CloseConnection();
      emit sig_connection_closed();
      return;
    }
    if (request == ID_CHAT_LOGIN) {
      CloseConnection();
      emit sig_login_failed(error);
    } else if (request == ID_SEARCH_USER_REQ && document.isObject() && error == UidInvalid) {
      emit sig_user_search(nullptr);
    } else if (id == ID_GET_AVATAR_RSP && document.isObject()) {
      emit sig_avatar_received(object["uid"].toInt(), {}, false);
    } else if (request == ID_UPDATE_PROFILE_REQ && document.isObject() && error == RedisError &&
               object["committed"].toBool()) {
      emit sig_profile_updated(object);
    } else if (request == ID_TEXT_CHAT_MSG_REQ && object["thread_id"].toInt() > 0) {
      emit sig_chat_msg_failed(object["thread_id"].toInt(), errorCodeMessage(error));
    } else if (request != 0) {
      failRequest(request, errorCodeMessage(error));
    }
    return;
  }
  const auto handler = _handlers.constFind(id);
  if (handler != _handlers.cend())
    handler.value()(object);
  else
    qWarning() << "Unhandled TCP message ID:" << id;
}

void TcpMgr::initHandlers()
{
  _handlers.insert(ID_CHAT_LOGIN_RSP, [this](const QJsonObject &o) {
    if (o["uid"].toInt() <= 0) {
      CloseConnection();
      emit sig_login_failed(UidInvalid);
      return;
    }
    auto mgr = UserMgr::GetInstance();
    mgr->Reset();
    mgr->SetUserInfo(std::make_shared<UserInfo>(o["uid"].toInt(), o["name"].toString(),
                                                o["nick"].toString(), o["icon"].toString(),
                                                o["sex"].toInt(), "", o["desc"].toString()));
    mgr->SetToken(o["token"].toString());
    mgr->AppendApplyList(o["apply_list"].toArray());
    mgr->AppendFriendList(o["friend_list"].toArray());
    _loggedIn = true;
    _messageDedup = o["message_dedup"].toBool(false);
    _requestCorrelation = o["request_correlation"].toBool(false);
    _lastHeartbeat.start();
    _heartbeatTimer.start();
    emit sig_swich_chatdlg();
  });
  _handlers.insert(ID_SET_CONTACT_TAGS_RSP, [this](const QJsonObject &o) {
    const int uid = o["friend_uid"].toInt();
    if (uid <= 0 || !o["tags"].isArray()) {
      emit sig_request_failed(ID_SET_CONTACT_TAGS_REQ, tr("标签响应格式错误"));
      return;
    }
    UserMgr::GetInstance()->SetFriendMetadata(uid, o);
    emit sig_contact_tags_updated(uid);
  });
  _handlers.insert(ID_SEARCH_USER_RSP, [this](const QJsonObject &o) {
    if (o["uid"].toInt() <= 0) {
      emit sig_user_search(nullptr);
      return;
    }
    emit sig_user_search(std::make_shared<SearchInfo>(o["uid"].toInt(), o["name"].toString(),
                                                      o["nick"].toString(), o["desc"].toString(),
                                                      o["sex"].toInt(), o["icon"].toString()));
  });
  _handlers.insert(ID_ADD_FRIEND_RSP,
                   [this](const QJsonObject &) { emit sig_add_friend_rsp(_applyTarget); });
  _handlers.insert(ID_NOTIFY_ADD_FRIEND_REQ, [this](const QJsonObject &o) {
    auto apply = std::make_shared<AddFriendApply>(o["applyuid"].toInt(), o["name"].toString(),
                                                  o["desc"].toString(), o["icon"].toString(),
                                                  o["nick"].toString(), o["sex"].toInt());
    UserMgr::GetInstance()->AddApplyList(std::make_shared<ApplyInfo>(apply));
    emit sig_friend_apply(apply);
  });
  _handlers.insert(ID_AUTH_FRIEND_RSP, [this](const QJsonObject &o) {
    auto auth =
        std::make_shared<AuthRsp>(o["uid"].toInt(), o["name"].toString(), o["nick"].toString(),
                                  o["icon"].toString(), o["sex"].toInt());
    auth->SetChatDatas(readMessages(o));
    if (auth->_thread_id == 0)
      auth->_thread_id = o["thread_id"].toInt();
    UserMgr::GetInstance()->AddFriend(auth);
    UserMgr::GetInstance()->SetFriendMetadata(auth->_uid, o);
    UserMgr::GetInstance()->MarkApplyAccepted(auth->_uid);
    emit sig_auth_rsp(auth);
  });
  _handlers.insert(ID_NOTIFY_AUTH_FRIEND_REQ, [this](const QJsonObject &o) {
    const int friendUid = o["fromuid"].toInt();
    const auto mgr = UserMgr::GetInstance();
    const auto outgoing = mgr->TakeOutgoingApply(friendUid);

    QString name = o["name"].toString();
    QString nick = o["nick"].toString();
    QString icon = o["icon"].toString();
    int sex = o["sex"].toInt();

    // 同一台 ChatServer 内转发时，旧服务端会把 uid/name/nick/icon 填成
    // 发起申请者自己；fromuid 才是真正通过申请的好友 uid。
    if (outgoing && o["uid"].toInt() == mgr->GetUid()) {
      name = outgoing->_name;
      nick = outgoing->_nick;
      icon = outgoing->_icon;
      sex = outgoing->_sex;
    }

    auto auth = std::make_shared<AuthInfo>(friendUid, name, nick, icon, sex);
    auth->SetChatDatas(readMessages(o));
    if (auth->_thread_id == 0)
      auth->_thread_id = o["thread_id"].toInt();
    mgr->AddFriend(auth);
    mgr->SetFriendMetadata(friendUid, o);
    emit sig_add_auth_friend(auth);
  });
  _handlers.insert(ID_TEXT_CHAT_MSG_RSP, [this](const QJsonObject &o) {
    emit sig_chat_msg_rsp(o["thread_id"].toInt(), readMessages(o));
  });
  _handlers.insert(ID_NOTIFY_TEXT_CHAT_MSG_REQ,
                   [this](const QJsonObject &o) { emit sig_text_chat_msg(readMessages(o)); });
  _handlers.insert(ID_LOAD_CHAT_THREAD_RSP, [this](const QJsonObject &o) {
    std::vector<std::shared_ptr<ChatThreadInfo>> threads;
    for (const auto &value : o["threads"].toArray()) {
      const auto item = value.toObject();
      auto thread = std::make_shared<ChatThreadInfo>();
      thread->_thread_id = item["thread_id"].toInt();
      thread->_type = item["type"].toString();
      thread->_user1_id = item["user1_id"].toInt();
      thread->_user2_id = item["user2_id"].toInt();
      thread->_last_message_id = item["last_message_id"].toInt();
      thread->_last_chat_time = item["last_chat_time"].toString();
      thread->_has_last_chat_summary =
          item.contains("last_message_id") && item.contains("last_chat_time");
      threads.push_back(thread);
    }
    emit sig_load_chat_thread(o["load_more"].toBool(), o["next_last_id"].toInt(), threads);
  });
  _handlers.insert(ID_CREATE_PRIVATE_CHAT_RSP, [this](const QJsonObject &o) {
    emit sig_create_private_chat(o["uid"].toInt(), o["other_id"].toInt(), o["thread_id"].toInt());
  });
  _handlers.insert(ID_LOAD_CHAT_MSG_RSP, [this](const QJsonObject &o) {
    bool valid = true;
    for (const auto &value : o["chat_datas"].toArray()) {
      const auto row = value.toObject();
      const auto content = row.contains("content") ? row["content"] : row["msg_content"];
      const int type = row["msg_type"].toInt(-1);
      if (!value.isObject() || !content.isString() || !row["chat_time"].isString() ||
          (type != 0 && type != 1))
        valid = false;
    }
    const auto messages = readMessages(o);
    if (!valid ||
        (!o["chat_datas"].isArray() && !o["chat_datas"].isNull() &&
         !o["chat_datas"].isUndefined()) ||
        messages.size() != size_t(o["chat_datas"].toArray().size()) ||
        !o["last_message_id"].isDouble() || !o["load_more"].isBool()) {
      emit sig_request_failed(ID_LOAD_CHAT_MSG_REQ, tr("历史消息格式错误，未推进本地同步位置"));
      return;
    }
    emit sig_load_chat_msg(o["thread_id"].toInt(), o["last_message_id"].toInt(),
                           o["load_more"].toBool(), messages);
  });
  _handlers.insert(ID_NOTIFY_OFF_LINE_REQ, [this](const QJsonObject &) {
    CloseConnection();
    emit sig_notify_offline();
  });
  _handlers.insert(ID_DELETE_FRIEND_RSP, [this](const QJsonObject &o) {
    if (o["friend_uid"].toInt() > 0)
      emit sig_friend_deleted(o["friend_uid"].toInt());
  });
  _handlers.insert(ID_NOTIFY_DELETE_FRIEND, [this](const QJsonObject &o) {
    if (o["uid"].toInt() == UserMgr::GetInstance()->GetUid() && o["friend_uid"].toInt() > 0)
      emit sig_friend_deleted(o["friend_uid"].toInt());
  });
  _handlers.insert(ID_HEARTBEAT_RSP, [this](const QJsonObject &o) {
    _lastHeartbeat.restart();
    // 跨服通知失败时，心跳中的数据库好友快照会补齐删除结果。
    if (!o["friend_ids"].isArray() || _heartbeatSnapshotId == 0 ||
        o["friend_snapshot_id"].toInt() != _heartbeatSnapshotId)
      return;
    QSet<int> friends;
    for (const auto &value : o["friend_ids"].toArray())
      friends.insert(value.toInt());
    QVector<int> removed;
    for (const auto &user : UserMgr::GetInstance()->GetFriendList())
      if (!friends.contains(user->_uid) && _heartbeatFriends.value(user->_uid) == user)
        removed.append(user->_uid);
    for (int uid : removed)
      emit sig_friend_deleted(uid);
  });
  _handlers.insert(ID_UPDATE_PROFILE_RSP,
                   [this](const QJsonObject &o) { emit sig_profile_updated(o); });
  _handlers.insert(ID_GET_AVATAR_RSP, [this](const QJsonObject &o) {
    emit sig_avatar_received(o["uid"].toInt(),
                             QByteArray::fromBase64(o["avatar"].toString().toLatin1()), true);
  });
}
