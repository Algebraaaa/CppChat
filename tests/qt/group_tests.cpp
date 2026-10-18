#include "app/thememanager.h"
#include "common/chatstore.h"
#include "common/runtimepaths.h"
#include "dialogs/chatpage.h"
#include "dialogs/chatsettingspanel.h"
#include "dialogs/chatuserwid.h"
#include "dialogs/contactssection.h"
#include "dialogs/groupdialog.h"
#include "dialogs/quickactionmenu.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/chatsyncservice.h"
#include "widgets/contactuserlist.h"
#include "widgets/searchlist.h"
#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
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
    QString error;
    QVERIFY2(ThemeManager::apply(*qApp, &error), qPrintable(error));
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
    auto activeGroup = group();
    bool failSecondInvite = true;
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
            reply["friend_list"] = QJsonArray{QJsonObject{{"uid", 102}, {"name", "friend"}},
                                              QJsonObject{{"uid", 103}, {"name", "Alice"}},
                                              QJsonObject{{"uid", 104}, {"name", "Bob"}}};
          } else if (id == ID_GROUP_REQ) {
            reply["op"] = request["op"];
            if (request["op"] == "add") {
              const int uid = request["member_uid"].toInt();
              if (uid == 104 && failSecondInvite) {
                reply["error"] = GroupForbidden;
                failSecondInvite = false;
              } else {
                auto members = activeGroup["members"].toArray();
                members.append(QJsonObject{{"uid", uid}, {"name", uid == 103 ? "Alice" : "Bob"}});
                activeGroup["members"] = members;
              }
            } else if (request["op"] == "rename")
              activeGroup["name"] = request["name"];
            else if (request["op"] == "transfer")
              activeGroup["owner_uid"] = request["member_uid"];
            reply["group"] = activeGroup;
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
    auto *choices = dialog.findChild<QListWidget *>("groupFriends");
    for (int i = 0; i < choices->count(); ++i)
      if (choices->item(i)->data(Qt::UserRole).toInt() == 102)
        choices->item(i)->setCheckState(Qt::Checked);
    QSignalSpy opened(&dialog, &GroupDialog::groupOpened);
    for (auto *button : dialog.findChildren<QPushButton *>())
      if (button->text() == QStringLiteral("创建"))
        button->click();
    QTRY_COMPARE(opened.count(), 1);
    QCOMPARE(opened.at(0).at(0).toInt(), 301);
    QCOMPARE(requests.last()["op"].toString(), QString("create"));
    QCOMPARE(requests.last()["name"].toString(), QStringLiteral("friend的群聊"));
    QCOMPARE(requests.last()["members"].toArray().size(), 1);
    QVERIFY(!requests.last()["creation_key"].toString().isEmpty());
    GroupDialog manage(301);
    QTRY_COMPARE(manage.findChild<QListWidget *>("groupMembers")->count(), 2);
    QCOMPARE(manage.findChild<QLineEdit *>("groupName")->text(), QStringLiteral("项目群"));
    QVERIFY(!manage.findChild<QLineEdit *>("groupName")->isReadOnly());
    auto *memberList = manage.findChild<QListWidget *>("groupMembers");
    auto *remove = manage.findChild<QPushButton *>("groupRemove");
    QVERIFY(!remove->isEnabled());
    memberList->setCurrentRow(0);
    QVERIFY(!remove->isEnabled()); // never offer kicking yourself
    memberList->setCurrentRow(1);
    QVERIFY(remove->isEnabled());
    auto *save = manage.findChild<QPushButton *>("groupSave");
    QVERIFY(!save->isEnabled()); // unchanged name is not a fake save
    manage.findChild<QLineEdit *>("groupName")->setText(QStringLiteral("协作群"));
    QVERIFY(save->isEnabled());
    save->click();
    QTRY_COMPARE(activeGroup["name"].toString(), QStringLiteral("协作群"));
    QTRY_VERIFY(!save->isEnabled());
    QTRY_COMPARE(manage.findChild<QLabel *>("groupStatus")->text(), QStringLiteral("群资料已更新"));
    auto *invite = manage.findChild<QPushButton *>("groupInvite");
    auto *friends = manage.findChild<QListWidget *>("groupFriends");
    QCOMPARE(friends->count(), 2);
    friends->item(0)->setCheckState(Qt::Checked);
    friends->item(1)->setCheckState(Qt::Checked);
    QVERIFY(invite->isEnabled());
    invite->click();
    QTRY_COMPARE(memberList->count(), 3);
    QTRY_VERIFY(
        manage.findChild<QLabel *>("groupStatus")->text().contains(QStringLiteral("其余未完成")));
    QCOMPARE(friends->count(), 1);
    QCOMPARE(friends->item(0)->checkState(), Qt::Checked); // failed selection retained
    invite->click();
    QTRY_COMPARE(memberList->count(), 4);
    QTRY_VERIFY(!invite->isEnabled());
    QCOMPARE(manage.findChild<QListWidget *>("groupSelected")->count(), 0);
    const auto directory = qEnvironmentVariable("CPPCHAT_TEST_ARTIFACTS");
    if (!directory.isEmpty()) {
      QVERIFY(QDir().mkpath(directory));
      manage.show();
      manage.findChild<QTabWidget *>("groupTabs")->setCurrentIndex(0);
      QTest::qWait(50);
      QVERIFY(manage.grab().save(directory + "/group-management.png"));
    }
    for (int i = 0; i < memberList->count(); ++i)
      if (memberList->item(i)->data(Qt::UserRole).toInt() == 103)
        memberList->setCurrentRow(i);
    QTimer::singleShot(0, [] {
      if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
        box->button(QMessageBox::Yes)->click();
    });
    manage.findChild<QPushButton *>("groupTransfer")->click();
    QTRY_VERIFY(manage.findChild<QLineEdit *>("groupName")->isReadOnly());
    QTRY_VERIFY(manage.findChild<QPushButton *>("groupInvite")->isHidden());
    QVERIFY(manage.findChild<QPushButton *>("groupRemove")->isHidden());
    QCOMPARE(manage.findChild<QPushButton *>("groupLeave")->text(), QStringLiteral("退出群聊"));
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
    QVERIFY(!create->isEnabled()); // invalid UTF-8 byte length does not issue a request
    name->setText(QStringLiteral("测试群"));
    create->click();
    QVERIFY(!create->isEnabled()); // at least one friend is required
    members->item(0)->setCheckState(Qt::Checked);
    QVERIFY(create->isEnabled());
    name->clear();
    QVERIFY(create->isEnabled()); // generated names remove an unnecessary creation hurdle
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
  void pickerSearchSelectionAndCapacity()
  {
    auto mgr = UserMgr::GetInstance();
    mgr->AddFriend(std::make_shared<UserInfo>(103, "Alice", "", "", 0, "", ""));
    mgr->GetFriendById(103)->_back = QStringLiteral("项目搭档");
    GroupDialog dialog;
    dialog.show();
    QTest::qWait(50);
    const auto *close = dialog.findChild<QPushButton *>("groupClose");
    QVERIFY(close->isVisible());
    QVERIFY(dialog.rect().contains(QRect(close->mapTo(&dialog, QPoint()), close->size())));
    auto *friends = dialog.findChild<QListWidget *>("groupFriends");
    auto *search = dialog.findChild<QLineEdit *>("groupSearch");
    auto *selected = dialog.findChild<QListWidget *>("groupSelected");
    auto *create = dialog.findChild<QPushButton *>("groupSave");
    search->setText(QStringLiteral("项目"));
    QTest::qWait(30);
    QVERIFY(friends->height() >= 140); // small/high-DPI screens scroll instead of crushing rows
    QListWidgetItem *match = nullptr;
    for (int i = 0; i < friends->count(); ++i)
      if (!friends->item(i)->isHidden())
        match = friends->item(i);
    QVERIFY(match);
    QCOMPARE(match->data(Qt::UserRole).toInt(), 103);
    QTest::mouseClick(friends->viewport(), Qt::LeftButton, Qt::NoModifier,
                      friends->visualItemRect(match).center());
    QCOMPARE(selected->count(), 1);
    QVERIFY(create->isEnabled());
    search->setText("friend");
    QVERIFY(match->isHidden());
    QCOMPARE(selected->count(), 1); // filtering is not deselection
    search->setText("nobody");
    QVERIFY(!dialog.findChild<QLabel *>("groupEmpty")->isHidden());
    search->clear();
    QTest::mouseClick(selected->viewport(), Qt::LeftButton, Qt::NoModifier,
                      selected->visualItemRect(selected->item(0)).center());
    QCOMPARE(match->checkState(), Qt::Unchecked);
    QCOMPARE(selected->count(), 0);
    QVERIFY(!create->isEnabled());
    const auto artifacts = qEnvironmentVariable("CPPCHAT_TEST_ARTIFACTS");
    if (!artifacts.isEmpty()) {
      match->setCheckState(Qt::Checked);
      QTest::qWait(50);
      QVERIFY(dialog.grab().save(artifacts + "/group-create.png"));
    }
    for (int i = 200; i < 232; ++i)
      mgr->AddFriend(std::make_shared<UserInfo>(i, QString::number(i), "", "", 0, "", ""));
    GroupDialog full;
    friends = full.findChild<QListWidget *>("groupFriends");
    for (int i = 0; i < friends->count(); ++i)
      friends->item(i)->setCheckState(Qt::Checked);
    QVERIFY(!full.findChild<QPushButton *>("groupSave")->isEnabled());
    QVERIFY(full.findChild<QLabel *>("groupStatus")->text().contains(QStringLiteral("超过")));
  }
  void groupDirectorySearchAndMessagePresentation()
  {
    ChatSyncService sync;
    sync.StartSession(101, server);
    QTRY_VERIFY(sync.IsReady());
    auto data = std::make_shared<ChatThreadData>(0, 301, 0);
    data->SetGroup(group());
    UserMgr::GetInstance()->AddChatThreadData(data, 0);
    ContactUserList directory;
    directory.Reload();
    QSignalSpy directoryOpen(&directory, &ContactUserList::sig_open_group);
    auto rows = directory.findItems(QStringLiteral("项目群"), Qt::MatchExactly);
    QCOMPARE(rows.size(), 1);
    directory.setCurrentItem(rows.first());
    QVERIFY(directory.ActivateCurrent());
    QCOMPARE(directoryOpen.count(), 1);
    QCOMPARE(directoryOpen.first().first().toInt(), 301);
    ContactsSection contacts;
    contacts.RefreshGroups();
    auto *contactsList = contacts.findChild<ContactUserList *>();
    QVERIFY(contactsList);
    QSignalSpy reset(contactsList->model(), &QAbstractItemModel::modelReset);
    contacts.RefreshGroups();
    QCOMPARE(reset.count(), 0); // periodic group snapshots must not rebuild the address book
    auto renamed = group();
    renamed["name"] = QStringLiteral("改名后的群");
    data->SetGroup(renamed);
    contacts.RefreshGroups();
    QCOMPARE(contactsList->findItems(QStringLiteral("改名后的群"), Qt::MatchExactly).size(), 1);
    data->SetGroup(group());
    SearchList search;
    search.Search(QStringLiteral("项目"), SearchList::Scope::Contacts);
    QVERIFY(search.findChild<ChatUserWid *>());
    UserMgr::GetInstance()->GetFriendById(102)->_back = QStringLiteral("项目搭档");
    data->AddMsg(std::make_shared<TextChatData>(1, 301, ChatFormType::PRIVATE, ChatMsgType::TEXT,
                                                QStringLiteral("明天开会"), 102, UN_READ,
                                                "2026-10-18 09:00:00"));
    ChatUserWid row;
    row.resize(300, 76);
    row.SetChatData(data);
    QCOMPARE(row.findChild<QLabel *>("user_chat_lb")->toolTip(),
             QStringLiteral("项目搭档: 明天开会"));
    QDialog host;
    host.setObjectName("ChatDialog");
    host.resize(780, 650);
    ChatPage page(&host);
    page.setObjectName("chat_page");
    page.resize(780, 650);
    page.SetChatData(data);
    host.show();
    QSignalSpy details(&page, &ChatPage::sig_group_settings);
    auto *title = page.findChild<QLabel *>("title_label");
    QCOMPARE(title->text(), QStringLiteral("项目群（2）"));
    QTest::mouseClick(title, Qt::LeftButton);
    QCOMPARE(details.count(), 1);
    QCOMPARE(details.first().first().toInt(), 301);
    auto *sender = page.findChild<QLabel *>("chat_user_name");
    QVERIFY(sender);
    QCOMPARE(sender->text(), QStringLiteral("项目搭档"));
    UserMgr::GetInstance()->GetFriendById(102)->_back = QStringLiteral("更新后的备注");
    page.SetChatData(data);
    QCOMPARE(sender->text(), QStringLiteral("更新后的备注"));
    QVERIFY(!page.findChild<QPushButton *>("file_btn")->isEnabled());
    auto *panel = page.findChild<ChatSettingsPanel *>();
    QSignalSpy manage(panel, &ChatSettingsPanel::sig_members_requested);
    panel->Toggle();
    auto *button = panel->findChild<QPushButton *>("groupDetailsButton");
    QVERIFY(button);
    button->click();
    QCOMPARE(manage.count(), 1);
    const auto artifacts = qEnvironmentVariable("CPPCHAT_TEST_ARTIFACTS");
    if (!artifacts.isEmpty()) {
      QTest::qWait(300);
      QVERIFY(page.grab().save(artifacts + "/group-chat.png"));
    }
    panel->Close(false);
    page.Reset();
    sync.ResetSession();
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
