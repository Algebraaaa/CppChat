#include "appbridge.h"
#include "mobilesession.h"
#include "common/chatstore.h"
#include "common/logger.h"
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QSettings>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSslKey>
#include <QSslServer>
#include <QTemporaryDir>
#include <QTest>

namespace {
QByteArray readFile(const QString &path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
void sendPacket(QSslSocket *socket, ReqId id, const QJsonObject &object)
{
  const auto body = QJsonDocument(object).toJson(QJsonDocument::Compact);
  QByteArray frame;
  QDataStream stream(&frame, QIODevice::WriteOnly);
  stream << quint16(id) << quint16(body.size());
  frame.append(body);
  socket->write(frame);
}
// Real TLS and existing protocol framing, with only synthetic users/messages.
// This fixture does not connect to a production server or change its data.
class SessionFixture : public QObject
{
public:
  QSslServer gate, chat;
  QPointer<QSslSocket> connection;
  int loginRequests = 0, chatLogins = 0, gateError = Success, latestMessage = 1;
  bool holdLogin = false;
  QString device;
  bool start(const QSslConfiguration &configuration)
  {
    gate.setSslConfiguration(configuration);
    chat.setSslConfiguration(configuration);
    connect(&gate, &QTcpServer::pendingConnectionAvailable, this, [this] {
      while (gate.hasPendingConnections()) {
        auto socket = qobject_cast<QSslSocket *>(gate.nextPendingConnection());
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QSslSocket::readyRead, this, [this, socket, buffer] {
          buffer->append(socket->readAll());
          const auto end = buffer->indexOf("\r\n\r\n");
          if (end < 0 || socket->property("answered").toBool())
            return;
          int length = 0;
          for (const auto &line : buffer->left(end).split('\n'))
            if (line.toLower().startsWith("content-length:"))
              length = line.mid(15).trimmed().toInt();
          if (buffer->size() < end + 4 + length)
            return;
          socket->setProperty("answered", true);
          ++loginRequests;
          if (holdLogin)
            return;
          const auto body = QJsonDocument(QJsonObject{{"error", gateError},
                                                      {"uid", 70001},
                                                      {"host", "127.0.0.1"},
                                                      {"port", int(chat.serverPort())},
                                                      {"token", "synthetic-test-token"}})
                                .toJson(QJsonDocument::Compact);
          socket->write(
              "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
              QByteArray::number(body.size()) + "\r\n\r\n" + body);
          socket->disconnectFromHost();
        });
      }
    });
    connect(&chat, &QTcpServer::pendingConnectionAvailable, this, [this] {
      while (chat.hasPendingConnections()) {
        auto socket = qobject_cast<QSslSocket *>(chat.nextPendingConnection());
        connection = socket;
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QSslSocket::readyRead, this, [this, socket, buffer] {
          buffer->append(socket->readAll());
          while (buffer->size() >= 4) {
            const auto id = ReqId((quint8((*buffer)[0]) << 8) | quint8((*buffer)[1]));
            const int length = (quint8((*buffer)[2]) << 8) | quint8((*buffer)[3]);
            if (buffer->size() < 4 + length)
              return;
            const auto request = QJsonDocument::fromJson(buffer->mid(4, length)).object();
            buffer->remove(0, 4 + length);
            if (id == ID_CHAT_LOGIN) {
              ++chatLogins;
              device = request["device"].toString();
              sendPacket(
                  socket, ID_CHAT_LOGIN_RSP,
                  {{"error", 0},
                   {"uid", 70001},
                   {"name", "synthetic"},
                   {"nick", "Recovery fixture"},
                   {"token", "synthetic-test-token"},
                   {"friend_list", QJsonArray{QJsonObject{{"uid", 70002}, {"name", "peer"}}}}});
            } else if (id == ID_LOAD_CHAT_THREAD_REQ) {
              sendPacket(socket, ID_LOAD_CHAT_THREAD_RSP,
                         {{"error", 0},
                          {"load_more", false},
                          {"next_last_id", 77},
                          {"threads",
                           QJsonArray{QJsonObject{{"thread_id", 77},
                                                  {"type", "private"},
                                                  {"user1_id", 70001},
                                                  {"user2_id", 70002},
                                                  {"last_message_id", latestMessage},
                                                  {"last_chat_time", "2026-10-23 01:00:00"}}}}});
            } else if (id == ID_LOAD_CHAT_MSG_REQ) {
              QJsonArray messages;
              for (int index = request["message_id"].toInt() + 1; index <= latestMessage; ++index)
                messages.append(QJsonObject{{"message_id", index},
                                            {"thread_id", 77},
                                            {"sender", 70002},
                                            {"msg_type", 0},
                                            {"content", "Synthetic recovery message"},
                                            {"chat_time", "2026-10-23 01:00:00"},
                                            {"unique_id", QString::number(index)}});
              sendPacket(socket, ID_LOAD_CHAT_MSG_RSP,
                         {{"error", 0},
                          {"thread_id", 77},
                          {"last_message_id", latestMessage},
                          {"load_more", false},
                          {"chat_datas", messages}});
            } else if (id == ID_HEART_BEAT_REQ) {
              sendPacket(socket, ID_HEARTBEAT_RSP, {{"error", 0}});
            }
          }
        });
      }
    });
    return gate.listen(QHostAddress::LocalHost) && chat.listen(QHostAddress::LocalHost);
  }
};
} // namespace

class MobileSessionTests : public QObject
{
  Q_OBJECT
private slots:
  void boundedRecoveryAndCancellation()
  {
    MobileSession session;
    QSignalSpy attempts(&session, &MobileSession::reconnectRequested);
    session.prepare("fixture@example.invalid", "SyntheticPassword2468");
    session.authenticated();
    session.setAvailable(false);
    session.interrupted();
    QVERIFY(session.active());
    QVERIFY(session.recovering());
    session.retryNow();
    QTest::qWait(20);
    QCOMPARE(attempts.count(), 0);
    session.setAvailable(true);
    QTRY_COMPARE(attempts.count(), 1);
    session.retryNow();
    QTest::qWait(20);
    QCOMPARE(attempts.count(), 1); // No overlapping login request.
    session.retryFailed();
    session.retryNow();
    QTRY_COMPARE(attempts.count(), 2);
    session.authenticated();
    QVERIFY(!session.recovering());
    session.interrupted();
    session.stop();
    session.retryNow();
    QTest::qWait(1300);
    QCOMPARE(attempts.count(), 2);
    QVERIFY(!session.active());
    QVERIFY(session.password().isEmpty());
  }
  void tlsRecoveryRetainsHistoryAndStopsOnLogoutOrRejection()
  {
    const auto root = qEnvironmentVariable("CPPCHAT_TEST_DATA");
    QVERIFY2(root.contains("/build/", Qt::CaseInsensitive) ||
                 root.contains("\\build\\", Qt::CaseInsensitive),
             "Use isolated build/ test data");
    QDir().mkpath(root);
    QTemporaryDir profile(root + "/profile-XXXXXX");
    QVERIFY(profile.isValid());
    auto closeLogger = qScopeGuard([&] {
      Logger::shutdown();
      for (const auto &file : QDir(profile.path() + "/logs").entryInfoList(QDir::Files))
        QFile::copy(file.absoluteFilePath(), root + '/' + file.fileName());
    });
    const auto tls = qEnvironmentVariable("CPPCHAT_TEST_TLS");
    const auto ca = QSslCertificate::fromData(readFile(tls + "/ca.pem"));
    QVERIFY(!ca.isEmpty());
    auto client = QSslConfiguration::defaultConfiguration();
    client.setCaCertificates(ca);
    QSslConfiguration::setDefaultConfiguration(client);
    auto server = client;
    server.setPeerVerifyMode(QSslSocket::VerifyNone);
    server.setLocalCertificate(QSslCertificate(readFile(tls + "/server.pem")));
    server.setPrivateKey(QSslKey(readFile(tls + "/server.key"), QSsl::Rsa));
    SessionFixture fixture;
    QVERIFY(fixture.start(server));
    QSettings settings(profile.path() + "/config.ini", QSettings::IniFormat);
    settings.setValue("GateServer/url",
                      "https://127.0.0.1:" + QString::number(fixture.gate.serverPort()));
    settings.setValue("ChatServer/tls", true);
    settings.sync();
    AppBridge bridge;
    QVERIFY(bridge.init(profile.path()));
    QVERIFY(bridge.storageInfo()["database"].toString().isEmpty());
    auto clearSecret = qScopeGuard([&] {
      QSignalSpy removed(bridge.account(), &MobileAccount::secretRemoved);
      bridge.account()->forget();
      removed.wait(10000);
    });
    QSignalSpy switched(&bridge, &AppBridge::chatSwitched);
    QSignalSpy logout(&bridge, &AppBridge::forceLogout);
    QSignalSpy errors(&bridge, &AppBridge::loginHttpResult);
    QSignalSpy drafts(&bridge, &AppBridge::draftLoaded);
    const auto login = [&] {
      bridge.loginUser("fixture@example.invalid", "SyntheticPassword2468");
    };
    login();
    QTRY_VERIFY_WITH_TIMEOUT(bridge.sessionActive() && bridge.storeReady(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(bridge.messagesForThread(77).size(), 1, 10000);
    QCOMPARE(fixture.device, QString("android"));
    QCOMPARE(switched.count(), 1);
    const int beforeNetworkHint = fixture.chatLogins;
    const auto originalSocket = fixture.connection;
    QVERIFY(QMetaObject::invokeMethod(&bridge, "networkTransportChanged", Qt::DirectConnection));
    QTest::qWait(300);
    QCOMPARE(fixture.connection, originalSocket);
    QCOMPARE(fixture.chatLogins, beforeNetworkHint);
    QVERIFY(bridge.isLoggedIn());
    QVERIFY(!bridge.reconnecting());
    bridge.account()->setAutomatic(true);
    MobileAccount remembered;
    remembered.initialize(settings.value("GateServer/url").toString());
    QTRY_VERIFY_WITH_TIMEOUT(remembered.ready(), 10000);
    QVERIFY(remembered.automatic());
    bridge.saveDraft(77, "Unsent synthetic draft");
    bridge.loadDraft(77);
    QTRY_COMPARE(drafts.count(), 1);
    QCOMPARE(drafts.last().at(1).toString(), QString("Unsent synthetic draft"));
    const auto directory = bridge.storageInfo()["directory"].toString();
    QVERIFY(!directory.isEmpty());

    fixture.gateError = RedisError; // A server outage must not disable login persistence.
    fixture.connection->abort();
    QTRY_VERIFY(bridge.reconnecting());
    QVERIFY(bridge.sessionActive());
    QVERIFY(bridge.storeReady());
    QCOMPARE(bridge.messagesForThread(77).size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.loginRequests >= 2 && !bridge.authBusy(), 8000);
    QVERIFY(bridge.sessionActive());
    QCOMPARE(logout.count(), 0);
    QCOMPARE(errors.count(), 0);
    QVERIFY(bridge.account()->automatic());
    fixture.latestMessage = 2;
    fixture.gateError = Success;
    bridge.reconnectNow();
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.reconnecting() && bridge.isLoggedIn() && bridge.storeReady(),
                             10000);
    QTRY_COMPARE_WITH_TIMEOUT(bridge.messagesForThread(77).size(), 2, 10000);
    QCOMPARE(switched.count(), 1); // Reconnection must not replace the current page.
    QCOMPARE(bridge.storageInfo()["directory"].toString(), directory);
    bridge.loadDraft(77);
    QTRY_COMPARE(drafts.count(), 2);
    QCOMPARE(drafts.last().at(1).toString(), QString("Unsent synthetic draft"));

    const int beforeResume = fixture.chatLogins;
    emit qGuiApp->applicationStateChanged(Qt::ApplicationSuspended);
    QTest::qWait(25100);
    emit qGuiApp->applicationStateChanged(Qt::ApplicationActive);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.chatLogins > beforeResume && !bridge.reconnecting(), 10000);
    QCOMPARE(switched.count(), 1);

    // Cancel during an outstanding HTTP login. No late response or timer can resurrect it.
    fixture.holdLogin = true;
    const int beforeCancel = fixture.loginRequests;
    bridge.reconnectNow();
    QTRY_VERIFY(bridge.authBusy());
    QTRY_VERIFY(fixture.loginRequests > beforeCancel);
    bridge.disconnectChat();
    QVERIFY(!bridge.sessionActive());
    QVERIFY(!bridge.authBusy());
    const int stopped = fixture.loginRequests;
    QTest::qWait(1400);
    QCOMPARE(fixture.loginRequests, stopped);
    QVERIFY(!bridge.reconnecting());

    fixture.holdLogin = false;
    login();
    QTRY_VERIFY_WITH_TIMEOUT(bridge.sessionActive() && bridge.storeReady(), 10000);
    const int beforeKick = fixture.loginRequests;
    sendPacket(fixture.connection, ID_NOTIFY_OFF_LINE_REQ, {{"error", 0}});
    QTRY_VERIFY(!bridge.sessionActive());
    QTest::qWait(1400);
    QCOMPARE(fixture.loginRequests, beforeKick);
    QVERIFY(!bridge.reconnecting());

    login();
    QTRY_VERIFY_WITH_TIMEOUT(bridge.sessionActive() && bridge.storeReady(), 10000);
    fixture.gateError = PasswdErr;
    bridge.reconnectNow();
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.sessionActive() && !bridge.authBusy(), 8000);
    const int rejected = fixture.loginRequests;
    QTest::qWait(1400);
    QCOMPARE(fixture.loginRequests, rejected);
    QVERIFY(!bridge.reconnecting());
    QVERIFY(logout.count() >= 3);

    // Startup automatic login has no active socket yet, but a temporary outage
    // must still retain the saved preference and recover without manual input.
    fixture.gateError = RedisError;
    bridge.account()->setAutomatic(true);
    bridge.startAutomaticLogin();
    QTRY_VERIFY_WITH_TIMEOUT(fixture.loginRequests > rejected && !bridge.authBusy(), 8000);
    QVERIFY(!bridge.sessionActive());
    QVERIFY(bridge.reconnecting());
    QVERIFY(bridge.account()->automatic());
    fixture.gateError = Success;
    bridge.reconnectNow();
    QTRY_VERIFY_WITH_TIMEOUT(bridge.sessionActive() && !bridge.reconnecting(), 10000);
    bridge.disconnectChat();
  }
};
QTEST_MAIN(MobileSessionTests)
#include "mobile_session.moc"
