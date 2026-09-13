#ifndef TCPMGR_H
#define TCPMGR_H
#include "common/global.h"
#include "common/singleton.h"
#include "common/userdata.h"
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QTimer>
#include <functional>

class TcpMgr : public QObject, public Singleton<TcpMgr> {
  Q_OBJECT
  friend class Singleton<TcpMgr>;
public:
  ~TcpMgr() override = default;
  void CloseConnection();
  bool IsConnected() const;
public slots:
  void slot_tcp_connect(ServerInfo server);
  void slot_send_data(ReqId id, QByteArray data);
signals:
  void sig_con_success(bool success);
  void sig_send_data(ReqId id, QByteArray data);
  void sig_swich_chatdlg();
  void sig_login_failed(int error);
  void sig_send_failed(ReqId id);
  void sig_request_failed(ReqId request, QString message);
  void sig_user_search(std::shared_ptr<SearchInfo> info);
  void sig_friend_apply(std::shared_ptr<AddFriendApply> apply);
  void sig_add_auth_friend(std::shared_ptr<AuthInfo> info);
  void sig_auth_rsp(std::shared_ptr<AuthRsp> info);
  void sig_add_friend_rsp(int uid);
  void sig_text_chat_msg(std::vector<std::shared_ptr<TextChatData>> messages);
  void sig_notify_offline();
  void sig_connection_closed();
  void sig_load_chat_thread(bool more, int lastId, std::vector<std::shared_ptr<ChatThreadInfo>> threads);
  void sig_create_private_chat(int uid, int otherId, int threadId);
  void sig_load_chat_msg(int threadId, int messageId, bool more, std::vector<std::shared_ptr<TextChatData>> messages);
  void sig_chat_msg_rsp(int threadId, std::vector<std::shared_ptr<TextChatData>> messages);
private:
  TcpMgr();
  void initHandlers();
  void readPackets();
  void handleMsg(ReqId id, const QByteArray &data);
  void failRequest(ReqId request, const QString &message);
  void startRequestTimer(ReqId request);
  void stopRequestTimer(ReqId request);
  void resetConnectionState();
  QTcpSocket _socket;
  QByteArray _buffer;
  bool _loggedIn = false;
  int _applyTarget = 0;
  QTimer _connectTimer;
  QTimer _heartbeatTimer;
  QElapsedTimer _lastHeartbeat;
  QMap<ReqId, QTimer *> _requestTimers;
  QMap<ReqId, QJsonObject> _pendingRequests;
  QMap<ReqId, std::function<void(const QJsonObject &)>> _handlers;
};
#endif // TCPMGR_H
