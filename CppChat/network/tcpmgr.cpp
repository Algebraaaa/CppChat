#include "tcpmgr.h"
#include "usermgr.h"
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonParseError>

namespace {
constexpr int kHeartbeatIntervalMs = 10000;
constexpr int kHeartbeatReplyTimeoutMs = 30000;

QString timeoutMessage(ReqId request)
{
  switch (request) {
  case ID_LOAD_CHAT_THREAD_REQ: return QObject::tr("会话列表加载超时，请重试");
  case ID_LOAD_CHAT_MSG_REQ: return QObject::tr("历史聊天记录加载超时，请重试");
  case ID_CREATE_PRIVATE_CHAT_REQ: return QObject::tr("创建聊天会话超时，请重试");
  case ID_SEARCH_USER_REQ: return QObject::tr("搜索用户超时，请重试");
  case ID_ADD_FRIEND_REQ: return QObject::tr("发送好友申请超时，请重试");
  case ID_AUTH_FRIEND_REQ: return QObject::tr("处理好友申请超时，请重试");
  case ID_CHAT_LOGIN: return QObject::tr("聊天服务器登录超时，请重试");
  default: return QObject::tr("服务器响应超时，请重试");
  }
}

// 回包编号与请求编号对应，通知包不属于任何客户端请求。
ReqId requestForResponse(ReqId response)
{
  switch (response) {
  case ID_CHAT_LOGIN_RSP: return ID_CHAT_LOGIN;
  case ID_SEARCH_USER_RSP: return ID_SEARCH_USER_REQ;
  case ID_ADD_FRIEND_RSP: return ID_ADD_FRIEND_REQ;
  case ID_AUTH_FRIEND_RSP: return ID_AUTH_FRIEND_REQ;
  case ID_TEXT_CHAT_MSG_RSP: return ID_TEXT_CHAT_MSG_REQ;
  case ID_LOAD_CHAT_THREAD_RSP: return ID_LOAD_CHAT_THREAD_REQ;
  case ID_CREATE_PRIVATE_CHAT_RSP: return ID_CREATE_PRIVATE_CHAT_REQ;
  case ID_LOAD_CHAT_MSG_RSP: return ID_LOAD_CHAT_MSG_REQ;
  default: return static_cast<ReqId>(0);
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
    if (id <= 0 || thread <= 0 || sender <= 0) continue;
    messages.push_back(std::make_shared<TextChatData>(id, msg["unique_id"].toString(),
        thread, ChatFormType::PRIVATE, ChatMsgType::TEXT, content, sender,
        msg["status"].toInt(), msg["chat_time"].toString()));
  }
  return messages;
}
}

TcpMgr::TcpMgr()
{
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
    if (wasLoggedIn) emit sig_connection_closed();
  });
  connect(&_heartbeatTimer, &QTimer::timeout, this, [this] {
    if (!_loggedIn) return;
    if (_lastHeartbeat.elapsed() >= kHeartbeatReplyTimeoutMs) {
      CloseConnection();
      emit sig_connection_closed();
      return;
    }
    const QJsonObject request{{"fromuid", UserMgr::GetInstance()->GetUid()}};
    slot_send_data(ID_HEART_BEAT_REQ, QJsonDocument(request).toJson(QJsonDocument::Compact));
  });
  connect(this, &TcpMgr::sig_send_data, this, &TcpMgr::slot_send_data);
  initHandlers();
}

bool TcpMgr::IsConnected() const
{ return _loggedIn && _socket.state() == QAbstractSocket::ConnectedState; }

void TcpMgr::resetConnectionState()
{
  _loggedIn = false;
  _connectTimer.stop();
  _heartbeatTimer.stop();
  for (auto *timer : _requestTimers) timer->stop();
  _pendingRequests.clear();
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
  _buffer.append(_socket.readAll());
  while (_buffer.size() >= 4) {
    quint16 id = 0, length = 0;
    QDataStream stream(_buffer.left(4));
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> id >> length;
    if (_buffer.size() < 4 + length) return;
    const auto body = _buffer.mid(4, length);
    _buffer.remove(0, 4 + length);
    handleMsg(static_cast<ReqId>(id), body);
  }
}

void TcpMgr::startRequestTimer(ReqId request)
{
  // 文本可以连续发送，逐条超时由 ChatPage 按 unique_id 管理。
  if (request == ID_TEXT_CHAT_MSG_REQ || request == ID_HEART_BEAT_REQ) return;
  if (!_requestTimers.contains(request)) {
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(15000);
    _requestTimers.insert(request, timer);
    connect(timer, &QTimer::timeout, this, [this, request] {
      failRequest(request, timeoutMessage(request));
    });
  }
  _requestTimers.value(request)->start();
}
void TcpMgr::stopRequestTimer(ReqId request)
{
  if (_requestTimers.contains(request)) _requestTimers.value(request)->stop();
  _pendingRequests.remove(request);
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
  if (body.size() > 2048
      || _socket.state() != QAbstractSocket::ConnectedState) {
    emit sig_send_failed(id);
    failRequest(id, tr("消息过大或聊天连接已断开"));
    return;
  }
  QByteArray packet;
  QDataStream stream(&packet, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::BigEndian);
  stream << static_cast<quint16>(id) << static_cast<quint16>(body.size());
  packet.append(body);
  if (id == ID_ADD_FRIEND_REQ) _applyTarget = QJsonDocument::fromJson(body).object()["touid"].toInt();
  if (id != ID_TEXT_CHAT_MSG_REQ && id != ID_HEART_BEAT_REQ)
    _pendingRequests.insert(id, QJsonDocument::fromJson(body).object());
  startRequestTimer(id);
  qInfo() << "TCP request queued:" << static_cast<int>(id) << "bytes:" << body.size();
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
    if (!_pendingRequests.contains(request)) return;
    const auto pending = _pendingRequests.value(request);
    if (request == ID_LOAD_CHAT_MSG_REQ && object.contains("thread_id")
        && object["thread_id"].toInt() != pending["thread_id"].toInt()) return;
    if (request == ID_CREATE_PRIVATE_CHAT_REQ && object.contains("other_id")
        && object["other_id"].toInt() != pending["other_id"].toInt()) return;
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
    } else if (request != 0) {
      failRequest(request, errorCodeMessage(error));
    }
    return;
  }
  const auto handler = _handlers.constFind(id);
  if (handler != _handlers.cend()) handler.value()(object);
  else qWarning() << "Unhandled TCP message ID:" << id;
}

void TcpMgr::initHandlers()
{
  _handlers.insert(ID_CHAT_LOGIN_RSP, [this](const QJsonObject &o) {
    if (o["uid"].toInt() <= 0) { CloseConnection(); emit sig_login_failed(UidInvalid); return; }
    auto mgr = UserMgr::GetInstance();
    mgr->Reset();
    mgr->SetUserInfo(std::make_shared<UserInfo>(o["uid"].toInt(), o["name"].toString(),
        o["nick"].toString(), o["icon"].toString(), o["sex"].toInt(), "", o["desc"].toString()));
    mgr->SetToken(o["token"].toString());
    mgr->AppendApplyList(o["apply_list"].toArray());
    mgr->AppendFriendList(o["friend_list"].toArray());
    _loggedIn = true;
    _lastHeartbeat.start();
    _heartbeatTimer.start();
    emit sig_swich_chatdlg();
  });
  _handlers.insert(ID_SEARCH_USER_RSP, [this](const QJsonObject &o) {
    if (o["uid"].toInt() <= 0) { emit sig_user_search(nullptr); return; }
    emit sig_user_search(std::make_shared<SearchInfo>(o["uid"].toInt(), o["name"].toString(),
        o["nick"].toString(), o["desc"].toString(), o["sex"].toInt(), o["icon"].toString()));
  });
  _handlers.insert(ID_ADD_FRIEND_RSP, [this](const QJsonObject &) { emit sig_add_friend_rsp(_applyTarget); });
  _handlers.insert(ID_NOTIFY_ADD_FRIEND_REQ, [this](const QJsonObject &o) {
    auto apply = std::make_shared<AddFriendApply>(o["applyuid"].toInt(), o["name"].toString(),
        o["desc"].toString(), o["icon"].toString(), o["nick"].toString(), o["sex"].toInt());
    UserMgr::GetInstance()->AddApplyList(std::make_shared<ApplyInfo>(apply));
    emit sig_friend_apply(apply);
  });
  _handlers.insert(ID_AUTH_FRIEND_RSP, [this](const QJsonObject &o) {
    auto auth = std::make_shared<AuthRsp>(o["uid"].toInt(), o["name"].toString(),
        o["nick"].toString(), o["icon"].toString(), o["sex"].toInt());
    auth->SetChatDatas(readMessages(o));
    if (auth->_thread_id == 0) auth->_thread_id = o["thread_id"].toInt();
    UserMgr::GetInstance()->AddFriend(auth);
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
    if (auth->_thread_id == 0) auth->_thread_id = o["thread_id"].toInt();
    mgr->AddFriend(auth);
    emit sig_add_auth_friend(auth);
  });
  _handlers.insert(ID_TEXT_CHAT_MSG_RSP, [this](const QJsonObject &o) {
    emit sig_chat_msg_rsp(o["thread_id"].toInt(), readMessages(o));
  });
  _handlers.insert(ID_NOTIFY_TEXT_CHAT_MSG_REQ, [this](const QJsonObject &o) {
    emit sig_text_chat_msg(readMessages(o));
  });
  _handlers.insert(ID_LOAD_CHAT_THREAD_RSP, [this](const QJsonObject &o) {
    std::vector<std::shared_ptr<ChatThreadInfo>> threads;
    for (const auto &value : o["threads"].toArray()) {
      const auto item = value.toObject();
      auto thread = std::make_shared<ChatThreadInfo>();
      thread->_thread_id = item["thread_id"].toInt();
      thread->_type = item["type"].toString();
      thread->_user1_id = item["user1_id"].toInt();
      thread->_user2_id = item["user2_id"].toInt();
      threads.push_back(thread);
    }
    emit sig_load_chat_thread(o["load_more"].toBool(), o["next_last_id"].toInt(), threads);
  });
  _handlers.insert(ID_CREATE_PRIVATE_CHAT_RSP, [this](const QJsonObject &o) {
    emit sig_create_private_chat(o["uid"].toInt(), o["other_id"].toInt(), o["thread_id"].toInt());
  });
  _handlers.insert(ID_LOAD_CHAT_MSG_RSP, [this](const QJsonObject &o) {
    emit sig_load_chat_msg(o["thread_id"].toInt(), o["last_message_id"].toInt(),
                           o["load_more"].toBool(), readMessages(o));
  });
  _handlers.insert(ID_NOTIFY_OFF_LINE_REQ, [this](const QJsonObject &) {
    CloseConnection();
    emit sig_notify_offline();
  });
  _handlers.insert(ID_HEARTBEAT_RSP, [this](const QJsonObject &) { _lastHeartbeat.restart(); });
}
