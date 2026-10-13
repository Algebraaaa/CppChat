#ifndef TCPMGR_H
#define TCPMGR_H
#include "common/global.h"
#include "common/singleton.h"
#include "common/userdata.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QMap>
#include <QSslSocket>
#include <QTimer>
#include <functional>

class TcpMgr : public QObject, public Singleton<TcpMgr> {
  Q_OBJECT
  friend class Singleton<TcpMgr>;

public:
  ~TcpMgr() override = default;
  void CloseConnection();
  bool IsConnected() const;
  bool SupportsMessageDedup() const { return _messageDedup; }
  void CancelRequest(ReqId request);
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
  void sig_load_chat_thread(bool more, int lastId,
                            std::vector<std::shared_ptr<ChatThreadInfo>> threads);
  void sig_create_private_chat(int uid, int otherId, int threadId);
  void sig_load_chat_msg(int threadId, int messageId, bool more,
                         std::vector<std::shared_ptr<TextChatData>> messages);
  void sig_chat_msg_rsp(int threadId, std::vector<std::shared_ptr<TextChatData>> messages);
  void sig_chat_msg_failed(int threadId, QString message);
  void sig_profile_updated(QJsonObject profile);
  void sig_avatar_received(int uid, QByteArray image, bool success);
  void sig_friend_deleted(int uid);
  void sig_contact_tags_updated(int uid);

private:
  TcpMgr();
  void initHandlers();
  void readPackets();
  void schedulePacketRead();
  void handleMsg(ReqId id, const QByteArray &data);
  void failRequest(ReqId request, const QString &message);
  void startRequestTimer(ReqId request);
  void stopRequestTimer(ReqId request);
  void resetConnectionState();
  QSslSocket _socket;
  bool _tlsEnabled = false;
  QByteArray _buffer;
  bool _readingPackets = false;
  bool _readScheduled = false;
  quint64 _connectionGeneration = 0;
  bool _loggedIn = false;
  bool _messageDedup = false;
  bool _requestCorrelation = false;
  int _applyTarget = 0;
  QTimer _connectTimer;
  QTimer _heartbeatTimer;
  QElapsedTimer _lastHeartbeat;
  int _heartbeatSnapshotId = 0;
  QMap<int, std::shared_ptr<UserInfo>> _heartbeatFriends;
  QMap<ReqId, QTimer *> _requestTimers;
  QMap<ReqId, QJsonObject> _pendingRequests;
  QMap<ReqId, std::function<void(const QJsonObject &)>> _handlers;
};
#endif // TCPMGR_H
