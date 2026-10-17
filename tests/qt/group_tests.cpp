#include "common/chatstore.h"
#include "common/runtimepaths.h"
#include "dialogs/chatpage.h"
#include "dialogs/chatuserwid.h"
#include "dialogs/groupdialog.h"
#include "dialogs/quickactionmenu.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/chatsyncservice.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

class GroupTests : public QObject {
  Q_OBJECT
  QTemporaryDir root;
  const QString server = "https://groups.example.test";
  QJsonObject group() const
  {
    return {{"thread_id", 301},
            {"name", QStringLiteral("项目群")},
            {"owner_uid", 101},
            {"members", QJsonArray{QJsonObject{{"uid", 101}, {"name", "self"}},
                                   QJsonObject{{"uid", 102}, {"name", QStringLiteral("小王")}}}}};
  }
private slots:
  void initTestCase()
  {
    QVERIFY(root.isValid());
    RuntimePaths::configure(root.filePath("config.ini"), root.path(), root.filePath("logs"));
    QFile config(root.filePath("config.ini"));
    QVERIFY(config.open(QIODevice::WriteOnly));
    config.write("[ChatServer]\ntls=false\n");
  }
  void init()
  {
    UserMgr::GetInstance()->Reset();
    UserMgr::GetInstance()->SetUserInfo(std::make_shared<UserInfo>(101, "self", "", "", 0, "", ""));
    UserMgr::GetInstance()->AddFriend(std::make_shared<UserInfo>(102, "friend", "", "", 0, "", ""));
  }
  void cleanup()
  {
    TcpMgr::GetInstance()->CloseConnection();
    TcpMgr::GetInstance()->CancelRequest(ID_GROUP_REQ);
    ChatStore::instance()->CloseAccount();
    UserMgr::GetInstance()->Reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void createAndManageThroughTcp()
  {
    QTcpServer serverSocket;
    QVERIFY(serverSocket.listen(QHostAddress::LocalHost));
    QPointer<QTcpSocket> socket;
    QByteArray input;
    QList<QJsonObject> requests;
    connect(&serverSocket, &QTcpServer::newConnection, this, [&] {
      socket = serverSocket.nextPendingConnection();
      connect(socket, &QTcpSocket::readyRead, &serverSocket, [&] {
        input += socket->readAll();
        while (input.size() >= 4) {
          const auto id = qFromBigEndian<quint16>(input.constData());
          const auto size = qFromBigEndian<quint16>(input.constData() + 2);
          if (input.size() < size + 4)
            return;
          auto request = QJsonDocument::fromJson(input.mid(4, size)).object();
          input.remove(0, size + 4);
          requests.append(request);
          QJsonObject reply{{"error", 0}, {"request_id", request["request_id"]}};
          if (id == ID_CHAT_LOGIN) {
            reply["uid"] = 101;
            reply["name"] = "self";
            reply["token"] = "isolated-fixture";
            reply["group_chat"] = true;
            reply["request_correlation"] = true;
            reply["friend_list"] = QJsonArray{QJsonObject{{"uid", 102}, {"name", "friend"}}};
          } else if (id == ID_GROUP_REQ) {
            reply["op"] = request["op"];
            reply["group"] = group();
          } else
            continue;
          const auto json = QJsonDocument(reply).toJson(QJsonDocument::Compact);
          QByteArray frame(4, '\0');
          qToBigEndian(static_cast<quint16>(id + 1), frame.data());
          qToBigEndian(static_cast<quint16>(json.size()), frame.data() + 2);
          socket->write(frame + json);
        }
      });
    });
    auto tcp = TcpMgr::GetInstance();
    QSignalSpy connected(tcp.get(), &TcpMgr::sig_con_success);
    tcp->slot_tcp_connect({"127.0.0.1", QString::number(serverSocket.serverPort()), {}, 101});
    QTRY_COMPARE(connected.count(), 1);
    tcp->slot_send_data(ID_CHAT_LOGIN,
                        QJsonDocument(QJsonObject{{"uid", 101}, {"token", "fixture"}}).toJson());
    QTRY_VERIFY(tcp->SupportsGroupChat());
    GroupDialog dialog;
    dialog.findChild<QLineEdit *>("groupName")->setText(QStringLiteral("项目群"));
    dialog.findChild<QListWidget *>("groupFriends")->item(0)->setCheckState(Qt::Checked);
    QSignalSpy opened(&dialog, &GroupDialog::groupOpened);
    for (auto *button : dialog.findChildren<QPushButton *>())
      if (button->text() == QStringLiteral("创建"))
        button->click();
    QTRY_COMPARE(opened.count(), 1);
    QCOMPARE(opened.at(0).at(0).toInt(), 301);
    QCOMPARE(requests.last()["op"].toString(), QString("create"));
    QCOMPARE(requests.last()["members"].toArray().size(), 1);
    QVERIFY(!requests.last()["creation_key"].toString().isEmpty());
    GroupDialog manage(301);
    QTRY_COMPARE(manage.findChild<QListWidget *>("groupMembers")->count(), 2);
    QCOMPARE(manage.findChild<QLineEdit *>("groupName")->text(), QStringLiteral("项目群"));
    QVERIFY(!manage.findChild<QLineEdit *>("groupName")->isReadOnly());
    const auto directory = qEnvironmentVariable("CPPCHAT_TEST_ARTIFACTS");
    if (!directory.isEmpty()) {
      QVERIFY(QDir().mkpath(directory));
      manage.show();
      QTest::qWait(50);
      QVERIFY(manage.grab().save(directory + "/group-management.png"));
    }
    manage.close();
    tcp->CloseConnection();
  }
  void createEntryAndValidation()
  {
    QuickActionMenu menu;
    QSignalSpy opened(&menu, &QuickActionMenu::createGroupRequested);
    menu.findChild<QPushButton *>("group_chat_btn")->click();
    QCOMPARE(opened.count(), 1);
    GroupDialog dialog;
    auto *members = dialog.findChild<QListWidget *>("groupFriends");
    QCOMPARE(members->count(), 1);
    auto *name = dialog.findChild<QLineEdit *>("groupName");
    name->setText(QString(22, QChar(0x4e2d)));
    QPushButton *create = nullptr;
    for (auto *button : dialog.findChildren<QPushButton *>())
      if (button->text() == QStringLiteral("创建"))
        create = button;
    QVERIFY(create);
    create->click();
    QVERIFY(create->isEnabled()); // invalid UTF-8 byte length does not issue a request
    name->setText(QStringLiteral("测试群"));
    create->click();
    QVERIFY(create->isEnabled()); // at least one friend is required
  }
  void groupListTitleAndSenderName()
  {
    auto data = std::make_shared<ChatThreadData>(0, 301, 0);
    data->SetGroup(group());
    QVERIFY(data->IsGroup());
    QCOMPARE(data->MemberName(102), QStringLiteral("小王"));
    UserMgr::GetInstance()->AddChatThreadData(data, 0);
    ChatUserWid item;
    item.SetChatData(data);
    bool found = false;
    for (auto *label : item.findChildren<QLabel *>())
      found |= label->text() == QStringLiteral("项目群");
    QVERIFY(found);
    data->SetGroup({{"name", QStringLiteral("改名后")}});
    QCOMPARE(data->GetGroupName(), QStringLiteral("改名后"));
    QCOMPARE(data->MemberName(102), QStringLiteral("小王"));
    QVERIFY(!UserMgr::GetInstance()->GetChatThreadByUid(0));
  }
  void encryptedGroupPersistenceAndRevocation()
  {
    ChatSyncService sync;
    sync.StartSession(101, server);
    QTRY_VERIFY(sync.IsReady());
    QSignalSpy changed(&sync, &ChatSyncService::conversationsChanged);
    emit TcpMgr::GetInstance() -> sig_group_info(group());
    QVERIFY(UserMgr::GetInstance()->GetChatThreadByThreadId(301));
    bool flushed = false;
    ChatStore::instance()->SaveConversations({}, [&](const ChatStoreResult &result) {
      QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
      flushed = true;
    });
    QTRY_VERIFY(flushed);
    sync.ResetSession();
    UserMgr::GetInstance()->RemoveGroup(301);
    sync.StartSession(101, server);
    QTRY_VERIFY(sync.IsReady());
    auto restored = UserMgr::GetInstance()->GetChatThreadByThreadId(301);
    QVERIFY(restored && restored->IsGroup());
    QCOMPARE(restored->GetGroupName(), QStringLiteral("项目群"));
    QCOMPARE(restored->MemberName(102), QStringLiteral("小王"));
    QSignalSpy removed(&sync, &ChatSyncService::groupRemoved);
    emit TcpMgr::GetInstance() -> sig_group_info({{"thread_id", 301}, {"removed", true}});
    QCOMPARE(removed.count(), 1);
    QVERIFY(!UserMgr::GetInstance()->GetChatThreadByThreadId(301));
    QVERIFY(UserMgr::GetInstance()->CheckFriendById(102));
    flushed = false;
    ChatStore::instance()->SaveConversations({}, [&](const ChatStoreResult &result) {
      QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
      flushed = true;
    });
    QTRY_VERIFY(flushed);
    sync.ResetSession();
    sync.StartSession(101, server);
    QTRY_VERIFY(sync.IsReady());
    QVERIFY(!UserMgr::GetInstance()->GetChatThreadByThreadId(301));
    sync.ResetSession();
  }
};
QTEST_MAIN(GroupTests)
#include "group_tests.moc"
