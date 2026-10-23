#include "appbridge.h"
#include "network/resourcemgr.h"
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>

// Credentials arrive only through the process environment. Output contains no
// passwords, tokens, message bodies or existing conversation contents.
int main(int argc, char **argv)
{
  QGuiApplication application(argc, argv);
  QCoreApplication::setOrganizationName("CppChatMobileTests");
  QCoreApplication::setApplicationName("Interop");
  AppBridge bridge;
  bridge.init(qEnvironmentVariable("CPPCHAT_TEST_DATA"));
  const auto peer = qEnvironmentVariableIntValue("CPPCHAT_TEST_PEER_UID");
  const auto marker = qEnvironmentVariable("CPPCHAT_TEST_MARKER");
  const bool peerRole = qEnvironmentVariable("CPPCHAT_TEST_ROLE") == "peer";
  const auto outbound = peerRole ? marker + "-peer" : marker;
  const auto inbound = peerRole ? marker : marker + "-peer";
  bool sent = false, confirmed = false, received = false;
  const bool testImages = qEnvironmentVariableIntValue("CPPCHAT_TEST_IMAGES") == 1;
  bool imageStarted = false, imageConfirmed = false, imageReceived = false;
  int imageBaseline = 0;
  QSet<QString> incomingImages;
  QByteArray imageHash;
  int thread = 0;
  auto output = [](const QJsonObject &object) {
    QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact) << Qt::endl;
  };
  QObject::connect(&bridge, &AppBridge::loginHttpResult, &application,
                   [&](int error, const QString &message) {
                     if (error) {
                       output({{"event", "login-failed"}, {"error", error}, {"message", message}});
                       application.exit(2);
                     }
                   });
  QObject::connect(&bridge, &AppBridge::forceLogout, &application, [&](const QString &) {
    output({{"event", "disconnected"}});
    application.exit(3);
  });
  QObject::connect(&bridge, &AppBridge::chatSwitched, &application, [&] {
    output({{"event", "authenticated"}, {"uid", bridge.currentUid()}});
    if (peer > 0)
      bridge.openChat(peer);
    else {
      output({{"event", "contacts"}, {"count", bridge.friendList().size()}});
      application.quit();
    }
  });
  QObject::connect(&bridge, &AppBridge::chatOpened, &application, [&](int id, const QString &) {
    thread = id;
    bridge.loadMessages(thread);
    QTimer::singleShot(12000, &application, [&] {
      sent = bridge.sendTextMessage(thread, outbound);
      output({{"event", "send-accepted"}, {"accepted", sent}});
    });
  });
  QObject::connect(&bridge, &AppBridge::messagesLoaded, &application, [&](int id) {
    if (id != thread || thread <= 0)
      return;
    for (const auto &value : bridge.messagesForThread(thread)) {
      const auto message = value.toMap();
      if (message.value("content").toString() == outbound && message.value("isSelf").toBool() &&
          message.value("msgId").toInt() > 0)
        confirmed = true;
      if (message.value("content").toString() == inbound && !message.value("isSelf").toBool())
        received = true;
    }
    if (imageStarted) {
      for (const auto &value : bridge.messagesForThread(thread)) {
        const auto message = value.toMap();
        if (message.value("type").toInt() != 1 || message.value("msgId").toInt() <= imageBaseline)
          continue;
        if (message.value("isSelf").toBool())
          imageConfirmed = true;
        else {
          const auto fileId = message.value("content").toString();
          incomingImages.insert(fileId);
          bridge.imageSource(fileId);
          const auto bytes = ResourceMgr::instance()->cachedImage(fileId);
          if (!bytes.isEmpty() &&
              QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) == imageHash)
            imageReceived = true;
        }
      }
    }
    if (confirmed && received && testImages && !imageStarted) {
      for (const auto &value : bridge.messagesForThread(thread))
        imageBaseline = qMax(imageBaseline, value.toMap().value("msgId").toInt());
      const auto path = qEnvironmentVariable("CPPCHAT_TEST_DATA") + "/interop-image.png";
      QImage image(16, 16, QImage::Format_RGB32);
      image.fill(QColor("#07a956"));
      image.save(path);
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly)) {
        application.exit(7);
        return;
      }
      imageHash = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
      imageStarted = true;
      if (!bridge.sendImage(thread, QUrl::fromLocalFile(path))) {
        output({{"event", "image-send-rejected"}});
        application.exit(5);
      }
    }
    if (confirmed && received && (!testImages || (imageConfirmed && imageReceived))) {
      output({{"event", "bidirectional-confirmed"}, {"thread", thread}});
      if (testImages)
        output({{"event", "image-sha256-confirmed"}});
      application.quit();
    }
  });
  QObject::connect(&bridge, &AppBridge::imageReady, &application, [&](const QString &id) {
    if (!incomingImages.contains(id))
      return;
    const auto bytes = ResourceMgr::instance()->cachedImage(id);
    if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) == imageHash) {
      imageReceived = true;
      if (imageConfirmed) {
        output({{"event", "bidirectional-confirmed"}, {"thread", thread}});
        output({{"event", "image-sha256-confirmed"}});
        application.quit();
      }
    } else {
      output({{"event", "image-hash-mismatch"}});
      application.exit(6);
    }
  });
  QObject::connect(&bridge, &AppBridge::operationFailed, &application, [&](const QString &message) {
    output({{"event", "operation-failed"}, {"message", message}});
  });
  QObject::connect(&bridge, &AppBridge::statusMessage, &application,
                   [&](const QString &) { output({{"event", "transfer-status"}}); });
  QTimer::singleShot(90000, &application, [&] {
    output(
        {{"event", "timeout"}, {"sent", sent}, {"confirmed", confirmed}, {"received", received}});
    application.exit(4);
  });
  bridge.loginUser(qEnvironmentVariable("CPPCHAT_TEST_EMAIL"),
                   qEnvironmentVariable("CPPCHAT_TEST_PASSWORD"));
  return application.exec();
}
