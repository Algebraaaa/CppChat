#include "common/attachmentpreparer.h"
#include "common/chatstore.h"
#include "common/fileattachment.h"
#include "common/runtimepaths.h"
#include "network/resourcemgr.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/chatsyncservice.h"
#include "services/messagesender.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QSslConfiguration>
#include <QTextStream>
#include <QTimer>

// Real shared desktop services and encrypted storage, using isolated TLS fixtures.
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  if (argc != 2)
    return 2;
  QFile config(QString::fromLocal8Bit(argv[1]));
  if (!config.open(QIODevice::ReadOnly))
    return 2;
  const auto settings = QJsonDocument::fromJson(config.readAll()).object();
  const auto directory = settings["directory"].toString();
  const int thread = settings["thread"].toInt();
  RuntimePaths::configure(directory + "/config.ini", directory, directory + "/logs");
  auto tls = QSslConfiguration::defaultConfiguration();
  tls.addCaCertificates(QSslCertificate::fromPath(settings["ca"].toString()));
  QSslConfiguration::setDefaultConfiguration(tls);
  ChatSyncService sync;
  MessageSender sender;
  const auto tcp = TcpMgr::GetInstance();
  QObject::connect(tcp.get(), &TcpMgr::sig_con_success, &app, [&](bool ok) {
    if (!ok) {
      app.exit(3);
      return;
    }
    tcp->slot_send_data(ID_CHAT_LOGIN, QJsonDocument(QJsonObject{{"uid", settings["uid"]},
                                                                 {"token", settings["token"]}})
                                           .toJson(QJsonDocument::Compact));
  });
  QObject::connect(tcp.get(), &TcpMgr::sig_swich_chatdlg, &sync, [&] {
    sync.StartSession(settings["uid"].toInt(), "https://isolated.example.invalid");
  });
  QObject::connect(&sync, &ChatSyncService::initializationFailed, &app,
                   [&](const QString &) { app.exit(8); });
  QByteArray payload;
  for (int i = 0; i < 1024; ++i)
    for (int byte = 0; byte < 256; ++byte)
      payload += char(byte);
  const auto path = directory + "/资料.bin";
  QFile input(path);
  if (!input.open(QIODevice::WriteOnly) || input.write(payload) != payload.size())
    return 4;
  input.close();
  const auto preparation = AttachmentPreparer::instance()->FromDocument(path);
  bool sent = false, confirmed = false, received = false;
  QString incoming;
  QObject::connect(ResourceMgr::instance(), &ResourceMgr::imageReady, &app,
                   [&](const QString &id, const QByteArray &bytes, const QString &failure) {
                     if (id != incoming)
                       return;
                     const auto document = FileAttachment::Unpack(bytes);
                     QString error;
                     if (!failure.isEmpty() || !document.valid || document.name != "资料.bin" ||
                         document.bytes != payload ||
                         !FileAttachment::Save(document, directory + "/received.bin", error)) {
                       app.exit(6);
                       return;
                     }
                     received = true;
                   });
  QTimer poll;
  poll.setInterval(100);
  QObject::connect(&poll, &QTimer::timeout, &app, [&] {
    const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(thread);
    if (!tcp->IsConnected() || !sync.IsReady() || !data || !preparation->ready)
      return;
    if (!sent) {
      MsgInfo file;
      file.msgFlag = "file";
      file.imageData = preparation->bytes;
      sent = preparation->error.isEmpty() && sender.Send({file}, data, false);
      if (!sent) {
        app.exit(5);
        return;
      }
    }
    for (const auto &message : data->GetMsgMapRef()) {
      if (message->GetMsgType() != ChatMsgType::FILE || message->GetMsgId() <= 0)
        continue;
      if (message->GetSendUid() == settings["uid"].toInt())
        confirmed = true;
      else if (incoming.isEmpty()) {
        incoming = message->GetMsgContent();
        ResourceMgr::instance()->fetchMedia(incoming);
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
  const int result = app.exec();
  sender.Reset();
  sync.ResetSession();
  ResourceMgr::instance()->ResetSession();
  tcp->CloseConnection();
  AttachmentPreparer::instance()->FinishPending();
  ChatStore::instance()->Shutdown();
  return result;
}
