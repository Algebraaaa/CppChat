#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include <QAbstractSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <limits>
TcpMgr::TcpMgr() : _host(""), _port(0), _b_recv_pending(false), _message_id(0), _message_len(0)
{
  QObject::connect(&_socket, &QTcpSocket::connected, [&]() {
    qDebug() << "Connected to server!";
    // 连接建立后发送消息
    emit sig_con_success(true);
  });
  QObject::connect(&_socket, &QAbstractSocket::errorOccurred, this, &TcpMgr::handleSocketError);

  // 当有新的网络数据到达，并且可以从 socket 中读取时，会发出 readyRead 信号
  // TCP 是字节流，一次 readyRead 可能对应：
  // 只有半个消息头	:保存下来，继续等
  // 消息头完整，:消息体只到一半	保存下来，继续等
  // 一条完整消息	:解析并分发
  // 多条消息连在一起	:循环解析，逐条分发
  QObject::connect(&_socket, &QTcpSocket::readyRead, [&]() {
    // 当有数据可读时，读取所有数据，并追加到缓冲区
    _buffer.append(_socket.readAll());

    constexpr int headerSize = static_cast<int>(sizeof(quint16) * 2);

    forever
    {
      // 先解析头部
      if (!_b_recv_pending) {
        // 检查缓冲区中的数据是否足够解析出一个消息头（消息ID + 消息长度）
        if (_buffer.size() < headerSize) {
          return; // 数据不够，等待更多数据
        }

        // 每次解析新包头都从当前缓冲区的起点创建数据流。
        // 这样一次收到多个包时，不会沿用上一个包头的读取位置。
        {
          QDataStream headerStream(&_buffer, QIODevice::ReadOnly);
          headerStream.setVersion(QDataStream::Qt_5_0);
          headerStream.setByteOrder(QDataStream::BigEndian);
          headerStream >> _message_id >> _message_len;
        }

        // 将buffer 中的前四个字节移除
        _buffer.remove(0, headerSize);

        // 输出读取的数据
        qDebug() << "Message ID:" << _message_id << ", Length:" << _message_len;
      }

      // buffer剩余长读是否满足消息体长度，不满足则退出继续等待接受
      if (_buffer.size() < _message_len) {
        _b_recv_pending = true;
        return;
      }

      _b_recv_pending = false;
      // 读取消息体
      QByteArray messageBody = _buffer.mid(0, _message_len);
      qDebug() << "Received message body, bytes:" << messageBody.size();

      _buffer.remove(0, _message_len);
      handleMsg(ReqId(_message_id), _message_len, messageBody);
    }
  });

  // 处理连接断开
  QObject::connect(&_socket, &QTcpSocket::disconnected,
                   [&]() { qDebug() << "Disconnected from server."; });
  // 连接发送信号用来发送数据
  QObject::connect(this, &TcpMgr::sig_send_data, this, &TcpMgr::slot_send_data);
  // 注册消息
  initHandlers();
}

TcpMgr::~TcpMgr() {}

void TcpMgr::handleSocketError(QAbstractSocket::SocketError error)
{
  qWarning() << "TCP socket error:" << error << _socket.errorString();

  switch (error) {
    // IP能找到，但目标端口没有程序监听。
  case QAbstractSocket::ConnectionRefusedError:
    // 服务器域名或地址无法找到
  case QAbstractSocket::HostNotFoundError:
    // 连接超时。
  case QAbstractSocket::SocketTimeoutError:
    emit sig_con_success(false);
    break;
  default:
    break;
  }
}

void TcpMgr::initHandlers()
{
  // auto self = shared_from_this();
  _handlers.insert(ID_CHAT_LOGIN_RSP, [this](ReqId id, int len, QByteArray data) {
    Q_UNUSED(len);
    qDebug() << "Handle message id:" << id << "bytes:" << data.size();
    // 将QByteArray转换为QJsonDocument
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

    // 检查转换是否成功
    if (jsonDoc.isNull()) {
      qDebug() << "Failed to create QJsonDocument.";
      return;
    }

    QJsonObject jsonObj = jsonDoc.object();

    if (!jsonObj.contains("error")) {
      int err = ErrorCodes::ERR_JSON;
      qDebug() << "Login Failed, err is Json Parse Err" << err;
      emit sig_login_failed(err);
      return;
    }

    int err = jsonObj["error"].toInt();
    if (err != ErrorCodes::SUCCESS) {
      qDebug() << "Login Failed, err is " << err;
      emit sig_login_failed(err);
      return;
    }

    UserMgr::GetInstance()->SetUid(jsonObj["uid"].toInt());
    UserMgr::GetInstance()->SetName(jsonObj["name"].toString());
    UserMgr::GetInstance()->SetToken(jsonObj["token"].toString());
    emit sig_swich_chatdlg();
  });
}

void TcpMgr::handleMsg(ReqId id, int len, QByteArray data)
{
  auto find_iter = _handlers.find(id);
  if (find_iter == _handlers.end()) {
    qDebug() << "not found id [" << id << "] to handle";
    return;
  }

  find_iter.value()(id, len, data);
}

void TcpMgr::slot_tcp_connect(ServerInfo si)
{
  qDebug() << "receive tcp connect signal";
  // 尝试连接到服务器
  qDebug() << "Connecting to server...";
  _host = si.Host;
  _port = static_cast<uint16_t>(si.Port.toUInt());
  _socket.connectToHost(_host, _port);
  // 成功后，QTcpSocket 会发出 QTcpSocket::connected信号
  // 如果没成功，则发出 QAbstractSocket::errorOccurred这个信号
}

void TcpMgr::slot_send_data(ReqId reqId, QString data)
{
  // 消息ID，占2字节，取值范围：0～65535
  const quint16 id = static_cast<quint16>(reqId);

  // 将字符串转换为UTF-8编码的字节数组
  const QByteArray dataBytes = data.toUtf8();

  if (dataBytes.size() > static_cast<qsizetype>(std::numeric_limits<quint16>::max())) {
    qWarning() << "TCP message is too large:" << dataBytes.size();
    return;
  }

  // 包头记录的是实际发送的 UTF-8 字节数，不是 QString 的字符数。
  const quint16 len = static_cast<quint16>(dataBytes.size());

  // 创建一个QByteArray用于存储要发送的所有数据
  QByteArray block;
  QDataStream out(&block, QIODevice::WriteOnly);

  // 设置数据流使用网络字节序
  out.setByteOrder(QDataStream::BigEndian);

  // 写入ID和长度
  out << id << len;

  // 添加字符串数据
  block.append(dataBytes);

  // 发送数据
  _socket.write(block);
}
