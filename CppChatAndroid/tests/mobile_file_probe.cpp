#include "appbridge.h"
#include "common/fileattachment.h"
#include "network/resourcemgr.h"
#include "network/tcpmgr.h"
#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSslConfiguration>
#include <QTextStream>
#include <QTimer>

// Controlled TLS loopback probe. Token/config are temporary fixture inputs.
int main(int argc, char **argv)
{
  QGuiApplication app(argc, argv);
  if (argc != 2)
    return 2;
  QFile config(QString::fromLocal8Bit(argv[1]));
  if (!config.open(QIODevice::ReadOnly))
    return 2;
  const auto settings = QJsonDocument::fromJson(config.readAll()).object();
  auto tls = QSslConfiguration::defaultConfiguration();
  tls.addCaCertificates(QSslCertificate::fromPath(settings["ca"].toString()));
  QSslConfiguration::setDefaultConfiguration(tls);
  const auto directory = settings["directory"].toString();
  const int thread = settings["thread"].toInt();
  AppBridge bridge;
  bridge.init(directory);
  const auto tcp = TcpMgr::GetInstance();
  QObject::connect(tcp.get(), &TcpMgr::sig_con_success, &app, [&](bool ok) {
    if (!ok) {
      app.exit(3);
      return;
    }
    tcp->slot_send_data(ID_CHAT_LOGIN, QJsonDocument(QJsonObject{
        {"uid", settings["uid"]}, {"token", settings["token"]}, {"device", "android"}}).toJson(QJsonDocument::Compact));
  });
  QByteArray payload;
  for (int i = 0; i < 1024; ++i)
    for (int byte = 0; byte < 256; ++byte)
      payload += char(byte);
  const auto path = directory + "/资料.bin";
  QFile input(path);
  if (!input.open(QIODevice::WriteOnly) || input.write(payload) != payload.size())
    return 4;
  input.close();
  bool sent = false, confirmed = false, received = false;
  QString incoming;
  QObject::connect(&bridge, &AppBridge::fileReady, &app, [&](const QString &id) {
    const auto document = FileAttachment::Unpack(ResourceMgr::instance()->cachedMedia(id));
    QString error;
    if (!document.valid || document.name != QString::fromUtf8("资料.bin") || document.bytes != payload ||
        !FileAttachment::Save(document, directory + "/received.bin", error)) {
      app.exit(6);
      return;
    }
    received = true;
  });
  QTimer poll;
  poll.setInterval(100);
  QObject::connect(&poll, &QTimer::timeout, &app, [&] {
    if (!bridge.isLoggedIn() || !bridge.storeReady())
      return;
    if (!sent && !bridge.threadInfo(thread).isEmpty()) {
      sent = bridge.sendFile(thread, QUrl::fromLocalFile(path));
      if (!sent) {
        app.exit(5);
        return;
      }
    }
    for (const auto &entry : bridge.messagesForThread(thread)) {
      const auto message = entry.toMap();
      if (message["type"].toInt() != 2 || message["msgId"].toInt() <= 0)
        continue;
      if (message["isSelf"].toBool())
        confirmed = true;
      else if (incoming.isEmpty()) {
        incoming = message["content"].toString();
        bridge.downloadFile(incoming);
      }
    }
    if (confirmed && received) {
      QTextStream(stdout) << "FILE_ROUNDTRIP_SHA256_OK" << Qt::endl;
      app.quit();
    }
  });
  poll.start();
  QTimer::singleShot(45000, &app, [&] { app.exit(7); });
  ServerInfo server;
  server.Host = "127.0.0.1";
  server.Port = QString::number(settings["port"].toInt());
  tcp->slot_tcp_connect(server);
  return app.exec();
}
