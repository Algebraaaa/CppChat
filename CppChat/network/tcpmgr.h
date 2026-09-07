#ifndef TCPMGR_H
#define TCPMGR_H
#include "common/global.h"
#include "common/singleton.h"
#include <QObject>
#include <QTcpSocket>
#include <functional>
class TcpMgr : public QObject,
               public Singleton<TcpMgr>,
               public std::enable_shared_from_this<TcpMgr> {
  Q_OBJECT
public:
  ~TcpMgr();

private:
  friend class Singleton<TcpMgr>;
  TcpMgr();
  void initHandlers();
  void handleMsg(ReqId id, int len, QByteArray data);
  void handleSocketError(QAbstractSocket::SocketError error);
  // 它是真正负责TCP通信的对象。主要操作包括：
  //_socket.connectToHost(host, port); // 连接服务器
  //_socket.write(data);               // 发送数据
  //_socket.readAll();                 // 读取数据
  QTcpSocket _socket;
  // GateServer返回的聊天服务器地址
  QString _host;
  uint16_t _port;
  // 收包缓冲区，TCP收到的数据不一定正好是一条完整消息，因此需要把每次收到的数据不断追加到 _buffer：
  QByteArray _buffer;
  // false：还没有解析消息头
  // true ：消息头已经解析完，正在等待完整消息体
  bool _b_recv_pending;
  quint16 _message_id;
  quint16 _message_len;
  // 消息ID到处理函数”的映射表：回包根据RequestID找到对应的处理函数
  QMap<ReqId, std::function<void(ReqId id, int len, QByteArray data)>> _handlers;
public slots:
  void slot_tcp_connect(ServerInfo);
  void slot_send_data(ReqId reqId, QString data);
signals:
  void sig_con_success(bool bsuccess);
  void sig_send_data(ReqId reqId, QString data);
  void sig_swich_chatdlg();
  void sig_login_failed(int);
};

#endif // TCPMGR_H
