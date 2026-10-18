#include "app/mainwindow.h"
#include "app/thememanager.h"
#include "common/autologinstore.h"
#include "common/contactnames.h"
#include "common/contactpreferences.h"
#include "common/runtimepaths.h"
#include "dialogs/applyfriend.h"
#include "dialogs/chatdialog.h"
#include "dialogs/commongroupsdialog.h"
#include "dialogs/friendinfopage.h"
#include "dialogs/groupdialog.h"
#include "dialogs/logindialog.h"
#include "dialogs/updatedialog.h"
#include "dialogs/voicecalldialog.h"
#include "network/httpmgr.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/clientupdateservice.h"
#include "services/voicecallservice.h"
#include "widgets/contactuserlist.h"
#include <QAction>
#include <QCheckBox>
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
#include <QToolButton>
#include <QtEndian>
#include <algorithm>
#include <memory>

namespace {
// Loopback-only scripted peers exercise real HTTP/TCP callbacks without production accounts.
class LoginPeer : public QObject {
public:
  QTcpServer http, chat;
  QList<QJsonObject> httpRequests, chatRequests;
  QPointer<QTcpSocket> chatSocket;
  int error = 0;
  bool holdHttp = false, holdChat = false;
  QJsonObject capabilities;
  std::function<void(QJsonObject)> groupHandler;
  LoginPeer()
  {
    http.listen(QHostAddress::LocalHost);
    chat.listen(QHostAddress::LocalHost);
    connect(&http, &QTcpServer::newConnection, this, [this] {
      auto *socket = http.nextPendingConnection();
      auto buffer = std::make_shared<QByteArray>();
      connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
        *buffer += socket->readAll();
        const int body = buffer->indexOf("\r\n\r\n");
        if (body < 0 || socket->property("handled").toBool())
          return;
        const auto document = QJsonDocument::fromJson(buffer->mid(body + 4));
        if (!document.isObject())
          return;
        socket->setProperty("handled", true);
        httpRequests.append(document.object());
        if (holdHttp)
          return;
        const auto response =
            QJsonDocument(QJsonObject{{"error", error},
                                      {"uid", 101},
                                      {"host", "127.0.0.1"},
                                      {"port", QString::number(chat.serverPort())},
                                      {"token", "fixture-session-token"}})
                .toJson(QJsonDocument::Compact);
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                      QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n" +
                      response);
        socket->disconnectFromHost();
      });
    });
    connect(&chat, &QTcpServer::newConnection, this, [this] {
      chatSocket = chat.nextPendingConnection();
      auto *socket = chatSocket.data();
      auto buffer = std::make_shared<QByteArray>();
      connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
        *buffer += socket->readAll();
        while (buffer->size() >= 4) {
          const auto id = qFromBigEndian<quint16>(buffer->constData());
          const auto size = qFromBigEndian<quint16>(buffer->constData() + 2);
          if (buffer->size() < size + 4)
            return;
          auto request = QJsonDocument::fromJson(buffer->mid(4, size)).object();
          request["id"] = id;
          chatRequests.append(request);
          buffer->remove(0, size + 4);
          if (id == ID_CHAT_LOGIN && !holdChat)
            completeLogin();
          if (id == ID_SET_CONTACT_TAGS_REQ) {
            request["error"] = 0;
            send(ID_SET_CONTACT_TAGS_RSP, request);
          }
          if (id == ID_GROUP_REQ && groupHandler)
            groupHandler(request);
        }
      });
    });
  }
  QString url() const { return QString("http://127.0.0.1:%1").arg(http.serverPort()); }
  void send(quint16 id, const QJsonObject &object)
  {
    const auto json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray frame(4, '\0');
    qToBigEndian(id, frame.data());
    qToBigEndian(static_cast<quint16>(json.size()), frame.data() + 2);
    if (chatSocket)
      chatSocket->write(frame + json);
  }
  void completeLogin()
  {
    QJsonObject response{{"error", 0},
                         {"uid", 101},
                         {"name", "fixture"},
                         {"token", "fixture-session-token"},
                         {"friend_list", QJsonArray()},
                         {"apply_list", QJsonArray()}};
    for (auto it = capabilities.begin(); it != capabilities.end(); ++it)
      response[it.key()] = it.value();
    send(ID_CHAT_LOGIN_RSP, response);
  }
};
} // namespace

class ClientFeatureTests : public QObject {
  Q_OBJECT
  std::unique_ptr<QTemporaryDir> root;
private slots:
  void init()
  {
    root = std::make_unique<QTemporaryDir>();
    QVERIFY(root->isValid());
    RuntimePaths::configure(root->filePath("config.ini"), root->path(), root->filePath("logs"));
    QFile config(root->filePath("config.ini"));
    QVERIFY(config.open(QIODevice::WriteOnly));
    config.write("[ChatServer]\ntls=false\n[ResourceServer]\nhost=127.0.0.1\nport=1\ntls=false\n");
    UserMgr::GetInstance()->Reset();
    gate_url_prefix = "http://127.0.0.1:1";
  }
  void cleanup()
  {
    HttpMgr::GetInstance()->CancelRequest(ID_LOGIN_USER, Modules::LOGINMOD);
    TcpMgr::GetInstance()->CloseConnection();
    UserMgr::GetInstance()->Reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    root.reset();
  }
  void pinyinInitials_data()
  {
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("initial");
    QTest::newRow("screenshot") << QString("砂糖白柚") << QString("S");
    QTest::newRow("surname-zhang") << QString("张三") << QString("Z");
    QTest::newRow("surname-li") << QString("李四") << QString("L");
    QTest::newRow("traditional") << QString("陳老師") << QString("C");
    QTest::newRow("mixed") << QString(" alice中文") << QString("A");
    QTest::newRow("polyphonic-default") << QString("重阳") << QString("Z");
    QTest::newRow("supplementary") << QString::fromUcs4(U"𠀀") << QString("H");
    QTest::newRow("number") << QString("123") << QString("#");
    QTest::newRow("emoji") << QString::fromUcs4(U"😀张") << QString("#");
    QTest::newRow("empty") << QString("  ") << QString("#");
  }
  void pinyinInitials()
  {
    QFETCH(QString, name);
    QFETCH(QString, initial);
    QCOMPARE(ContactNames::initial(name), initial);
  }
  void contactsUseRemarkAndRegroup()
  {
    auto mgr = UserMgr::GetInstance();
    auto user = std::make_shared<UserInfo>(102, "砂糖白柚", ":/res/head_1.jpg");
    mgr->AddFriend(user);
    ContactUserList list;
    list.Reload();
    QVERIFY(!list.findItems("S", Qt::MatchExactly).isEmpty());
    mgr->SetFriendMetadata(102, {{"back", "李同学"}});
    list.Reload();
    QVERIFY(!list.findItems("李同学", Qt::MatchExactly).isEmpty());
    QVERIFY(!list.findItems("L", Qt::MatchExactly).isEmpty());
    QVERIFY(list.findItems("砂糖白柚", Qt::MatchExactly).isEmpty());
    mgr->SetFriendMetadata(102, {{"back", ""}});
    list.Reload();
    QVERIFY(!list.findItems("砂糖白柚", Qt::MatchExactly).isEmpty());
    QVERIFY(!list.findItems("S", Qt::MatchExactly).isEmpty());
  }
  void remarkEditorAndMenuAreAvailable()
  {
    auto user = std::make_shared<UserInfo>(102, "fixture", ":/res/head_1.jpg");
    user->_back = "同学";
    ApplyFriend dialog;
    dialog.SetContactInfo(user);
    auto *field = dialog.findChild<QLineEdit *>("back_ed");
    QVERIFY(field && !field->isHidden());
    QCOMPARE(field->text(), user->_back);
    QCOMPARE(field->maxLength(), 64);
    FriendInfoPage profile;
    QVERIFY(profile.findChild<QAction *>("edit_contact_metadata_action"));
  }
  void starredContactsPersistAndStayAccountIsolated()
  {
    auto mgr = UserMgr::GetInstance();
    gate_url_prefix = "https://contacts.example.test";
    const auto self = std::make_shared<UserInfo>(101, "self", "");
    const auto user = std::make_shared<UserInfo>(102, "friend", "");
    user->_back = "项目同学";
    mgr->SetUserInfo(self);
    mgr->AddFriend(user);
    ContactUserList list;
    list.Reload();
    FriendInfoPage profile;
    profile.SetInfo(user);
    auto *action = profile.findChild<QAction *>("star_contact_action");
    QVERIFY(action && action->isEnabled());
    action->trigger();
    QVERIFY(mgr->IsStarredFriend(102));
    QVERIFY(action->isChecked());
    QCOMPARE(list.findItems("项目同学", Qt::MatchExactly).size(), 2);
    const auto artifacts = qEnvironmentVariable("CPPCHAT_TEST_ARTIFACTS");
    if (!artifacts.isEmpty()) {
      QString error;
      QVERIFY2(ThemeManager::apply(*qApp, &error), qPrintable(error));
      list.resize(320, 720);
      list.show();
      QVERIFY(list.grab().save(artifacts + "/starred-contacts.png"));
    }
    auto *header = list.findItems("星标朋友", Qt::MatchExactly).first();
    auto *starredRow = list.item(list.row(header) + 1);
    QCOMPARE(starredRow->text(), QString("项目同学"));
    QSignalSpy selected(&list, &ContactUserList::sig_switch_friend_info_page);
    list.setCurrentItem(starredRow);
    QVERIFY(list.ActivateCurrent());
    QCOMPARE(selected.size(), 1);
    list.itemClicked(header);
    QVERIFY(starredRow->isHidden());
    list.Reload();
    header = list.findItems("星标朋友", Qt::MatchExactly).first();
    QVERIFY(list.item(list.row(header) + 1)->isHidden());
    mgr->Reset();
    mgr->SetUserInfo(self);
    mgr->AddFriend(user);
    QVERIFY(mgr->IsStarredFriend(102));
    mgr->SetUserInfo(std::make_shared<UserInfo>(201, "other", ""));
    QVERIFY(!mgr->IsStarredFriend(102));
    gate_url_prefix = "https://another.example.test";
    mgr->SetUserInfo(self);
    QVERIFY(!mgr->IsStarredFriend(102));
    gate_url_prefix = "https://contacts.example.test";
    mgr->SetUserInfo(self);
    QVERIFY(mgr->IsStarredFriend(102));
    mgr->RemoveFriend(102);
    mgr->Reset();
    mgr->SetUserInfo(self);
    mgr->AddFriend(user);
    QVERIFY(!mgr->IsStarredFriend(102));
    profile.SetInfo(user);
    action->trigger();
    action->trigger();
    QVERIFY(!mgr->IsStarredFriend(102));
    QCOMPARE(list.findItems("项目同学", Qt::MatchExactly).size(), 1);
  }
  void starredWriteFailureDoesNotPretendToSucceed()
  {
    auto mgr = UserMgr::GetInstance();
    mgr->SetUserInfo(std::make_shared<UserInfo>(101, "self", ""));
    auto user = std::make_shared<UserInfo>(102, "friend", "");
    mgr->AddFriend(user);
    QVERIFY(QDir().mkpath(RuntimePaths::preferencesDirectory() + "/contacts.ini"));
    FriendInfoPage profile;
    profile.SetInfo(user);
    auto *action = profile.findChild<QAction *>("star_contact_action");
    action->trigger();
    QVERIFY(!mgr->IsStarredFriend(102));
    QVERIFY(!action->isChecked());
    QVERIFY(ContactPreferences::LoadStarred(101, gate_url_prefix).isEmpty());
    QVERIFY(!profile.findChild<QLabel *>("delete_status")->text().isEmpty());
  }
  void contactProfileStartsTheExistingCall_data()
  {
    QTest::addColumn<bool>("video");
    QTest::newRow("audio") << false;
    QTest::newRow("video") << true;
  }
  void contactProfileStartsTheExistingCall()
  {
    QFETCH(bool, video);
    LoginPeer peer;
    peer.capabilities = {{"voice_call", true}, {"video_call", true}};
    gate_url_prefix = peer.url();
    LoginDialog login;
    login.findChild<QLineEdit *>("email_edit")->setText("user@example.invalid");
    login.findChild<QLineEdit *>("pass_edit")->setText("Password123");
    login.findChild<QPushButton *>("login_btn")->click();
    QTRY_VERIFY(TcpMgr::GetInstance()->IsConnected());
    auto user = std::make_shared<UserInfo>(102, "friend", "");
    UserMgr::GetInstance()->AddFriend(user);
    ChatDialog chat;
    auto *profile = chat.findChild<FriendInfoPage *>();
    profile->SetInfo(user);
    auto *button = profile->findChild<QToolButton *>(video ? "video_call_btn" : "voice_call_btn");
    QVERIFY(button->isEnabled());
    button->click();
    auto *call = chat.findChild<VoiceCallService *>();
    QCOMPARE(call->state(), VoiceCallService::State::Outgoing);
    QCOMPARE(call->peer(), 102);
    QCOMPARE(call->isVideo(), video);
    QTRY_VERIFY(
        std::any_of(peer.chatRequests.cbegin(), peer.chatRequests.cend(), [video](const auto &r) {
          return r["id"].toInt() == ID_VOICE_REQ && r["action"] == "invite" &&
                 r["peer"].toInt() == 102 && r["video"].toBool() == video;
        }));
    chat.findChild<VoiceCallDialog *>()->reject();
    QCOMPARE(call->state(), VoiceCallService::State::Idle);
    peer.chatSocket->disconnectFromHost();
    QTRY_VERIFY(!button->isEnabled());
  }
  void commonGroupsUseTcpPaginationRetryAndDoNotReplaceOtherRequests()
  {
    LoginPeer peer;
    peer.capabilities = {
        {"common_groups", true}, {"group_chat", true}, {"request_correlation", true}};
    // The first login was sent before correlation support was negotiated.
    gate_url_prefix = peer.url();
    LoginDialog login;
    login.findChild<QLineEdit *>("email_edit")->setText("user@example.invalid");
    login.findChild<QLineEdit *>("pass_edit")->setText("Password123");
    login.findChild<QPushButton *>("login_btn")->click();
    QTRY_VERIFY(TcpMgr::GetInstance()->IsConnected());
    UserMgr::GetInstance()->AddFriend(std::make_shared<UserInfo>(102, "friend", ""));
    bool failPage = true;
    QJsonObject held;
    peer.groupHandler = [&](QJsonObject request) { held = request; };
    CommonGroupsDialog dialog(102);
    dialog.show();
    QTRY_VERIFY(!held.isEmpty());
    QCOMPARE(held["op"].toString(), QString("common"));
    const int count = peer.chatRequests.size();
    GroupDialog competing(301);
    competing.show();
    QTest::qWait(20);
    QCOMPARE(peer.chatRequests.size(), count);
    competing.close();
    auto respond = [&](QJsonObject request) {
      const int after = request["after"].toInt();
      QJsonObject result{
          {"friend_uid", 102},
          {"has_more", after == 0},
          {"next_thread_id", after == 0 ? 301 : 303},
          {"groups", QJsonArray{QJsonObject{{"thread_id", after == 0 ? 301 : 303},
                                            {"name", after == 0 ? "共同项目" : "讨论群"},
                                            {"owner_uid", 101}}}}};
      const bool fail = after != 0 && failPage;
      if (fail)
        failPage = false;
      peer.send(ID_GROUP_RSP, {{"error", fail ? int(DatabaseError) : 0},
                               {"request_id", request["request_id"]},
                               {"op", "common"},
                               {"group", result}});
    };
    peer.groupHandler = respond;
    respond(held);
    auto *list = dialog.findChild<QListWidget *>("commonGroupsList");
    QTRY_COMPARE(list->count(), 1);
    auto *more = dialog.findChild<QPushButton *>("loadMoreCommonGroups");
    more->click();
    QTRY_VERIFY(more->isEnabled());
    QCOMPARE(list->count(), 1);
    more->click();
    QTRY_COMPARE(list->count(), 2);
    QVERIFY(!more->isEnabled());
    const auto artifacts = qEnvironmentVariable("CPPCHAT_TEST_ARTIFACTS");
    if (!artifacts.isEmpty())
      QVERIFY(dialog.grab().save(artifacts + "/common-groups.png"));
    QSignalSpy opened(&dialog, &CommonGroupsDialog::groupOpened);
    list->setCurrentRow(1);
    dialog.findChild<QPushButton *>("openCommonGroup")->click();
    QCOMPARE(opened.size(), 1);
    QCOMPARE(opened.first().first().toInt(), 303);
    QVERIFY(!TcpMgr::GetInstance()->HasPendingRequest(ID_GROUP_REQ));
    TcpMgr::GetInstance()->CloseConnection();
    QVERIFY(!TcpMgr::GetInstance()->SupportsCommonGroups());
  }
  void unavailableServerAndClosingCommonGroupsStayLocal()
  {
    CommonGroupsDialog dialog(102);
    QSignalSpy requests(TcpMgr::GetInstance().get(), &TcpMgr::sig_send_data);
    dialog.show();
    QTest::qWait(10);
    QCOMPARE(requests.size(), 0);
    QVERIFY(dialog.findChild<QLabel *>("commonGroupsStatus")->text().contains("未支持"));
    dialog.close();
    CommonGroupsDialog closedBeforeLoad(102);
    closedBeforeLoad.show();
    closedBeforeLoad.close();
    QTest::qWait(10);
    QCOMPARE(requests.size(), 0);
    FriendInfoPage profile;
    profile.SetInfo(nullptr);
    QVERIFY(!profile.findChild<QToolButton *>("voice_call_btn")->isEnabled());
    QVERIFY(!profile.findChild<QToolButton *>("video_call_btn")->isEnabled());
  }
#ifdef Q_OS_WIN
  void encryptedCredentialsAreIsolatedAndRejectTampering()
  {
    if (!AutoLoginStore::supported())
      QSKIP("Secure automatic login currently requires Windows DPAPI");
    const QUrl first("https://one.example.test"), other("https://two.example.test");
    AutoLoginStore::save(first, {"user@example.invalid", "Password123"});
    QVERIFY(AutoLoginStore::load(first).has_value());
    QVERIFY(!AutoLoginStore::load(other));
    const QDir folder(RuntimePaths::preferencesDirectory() + "/auto-login");
    const auto files = folder.entryList({"*.bin"}, QDir::Files);
    QCOMPARE(files.size(), 1);
    QFile file(folder.filePath(files.first()));
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto bytes = file.readAll();
    QVERIFY(!bytes.contains("Password123") && !bytes.contains("user@example.invalid"));
    file.close();
    AutoLoginStore::save(other, {"user@example.invalid", "Password123"});
    for (const auto &name : folder.entryList({"*.bin"}, QDir::Files)) {
      if (name == files.first())
        continue;
      QFile copied(folder.filePath(name));
      QVERIFY(copied.open(QIODevice::WriteOnly));
      QCOMPARE(copied.write(bytes), bytes.size());
      copied.close();
      QVERIFY_EXCEPTION_THROWN(AutoLoginStore::load(other), std::exception);
    }
    AutoLoginStore::clear(other);
    bytes[bytes.size() - 1] = char(bytes.back() ^ 0x80);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();
    QVERIFY_EXCEPTION_THROWN(AutoLoginStore::load(first), std::exception);
    AutoLoginStore::clear(first);
    QVERIFY(!AutoLoginStore::load(first));
  }
  void successfulLoginPersistsOnlyAfterChatAuthenticationAndRestartsAutomatically()
  {
    if (!AutoLoginStore::supported())
      QSKIP("Secure automatic login currently requires Windows DPAPI");
    LoginPeer peer;
    gate_url_prefix = peer.url();
    peer.holdChat = true;
    auto login = std::make_unique<LoginDialog>();
    login->findChild<QLineEdit *>("email_edit")->setText("user@example.invalid");
    login->findChild<QLineEdit *>("pass_edit")->setText("Password123");
    login->findChild<QPushButton *>("login_btn")->click();
    QTRY_COMPARE(peer.chatRequests.size(), 1);
    QVERIFY(!AutoLoginStore::load(QUrl(peer.url())));
    peer.completeLogin();
    QTRY_VERIFY(AutoLoginStore::load(QUrl(peer.url())).has_value());
    login.reset();
    TcpMgr::GetInstance()->CloseConnection();
    UserMgr::GetInstance()->Reset();
    peer.holdChat = false;
    login = std::make_unique<LoginDialog>();
    login->TryAutoLogin();
    login->TryAutoLogin();
    QTRY_COMPARE(peer.httpRequests.size(), 2);
    QTRY_COMPARE(UserMgr::GetInstance()->GetUid(), 101);
    QCOMPARE(peer.httpRequests.back()["email"].toString(), QString("user@example.invalid"));
    QCOMPARE(peer.httpRequests.back()["passwd"].toString(), QString("Password123"));
    login->ResetForLogin("已退出登录", true);
    QVERIFY(!AutoLoginStore::load(QUrl(peer.url())));
  }
  void cancelAndDisableAutomaticLogin()
  {
    if (!AutoLoginStore::supported())
      QSKIP("Secure automatic login currently requires Windows DPAPI");
    LoginPeer peer;
    peer.holdHttp = true;
    gate_url_prefix = peer.url();
    AutoLoginStore::save(QUrl(peer.url()), {"user@example.invalid", "Password123"});
    LoginDialog login;
    login.TryAutoLogin();
    QTRY_COMPARE(peer.httpRequests.size(), 1);
    auto *cancel = login.findChild<QPushButton *>("cancel_auto_login_btn");
    QVERIFY(cancel && !cancel->isHidden());
    cancel->click();
    QVERIFY(login.findChild<QPushButton *>("login_btn")->isEnabled());
    login.TryAutoLogin();
    QTest::qWait(30);
    QCOMPARE(peer.httpRequests.size(), 1);
    login.findChild<QCheckBox *>("auto_login_check")->setChecked(false);
    QVERIFY(!AutoLoginStore::load(QUrl(peer.url())));
  }
  void invalidPasswordClearsCredentialsButServiceFailureDoesNot_data()
  {
    QTest::addColumn<int>("error");
    QTest::addColumn<bool>("retained");
    QTest::newRow("password-changed") << int(ErrorCodes::PasswdInvalid) << false;
    QTest::newRow("database-outage") << int(ErrorCodes::DatabaseError) << true;
  }
  void invalidPasswordClearsCredentialsButServiceFailureDoesNot()
  {
    if (!AutoLoginStore::supported())
      QSKIP("Secure automatic login currently requires Windows DPAPI");
    QFETCH(int, error);
    QFETCH(bool, retained);
    LoginPeer peer;
    peer.error = error;
    gate_url_prefix = peer.url();
    AutoLoginStore::save(QUrl(peer.url()), {"user@example.invalid", "Password123"});
    LoginDialog login;
    login.TryAutoLogin();
    QTRY_VERIFY(login.findChild<QPushButton *>("login_btn")->isEnabled());
    QCOMPARE(AutoLoginStore::load(QUrl(peer.url())).has_value(), retained);
    QCOMPARE(peer.httpRequests.size(), 1);
  }
#endif
  void remarkRoundTripUpdatesTheContactObjectOnlyAfterAcknowledgment()
  {
    LoginPeer peer;
    gate_url_prefix = peer.url();
    LoginDialog login;
    login.findChild<QLineEdit *>("email_edit")->setText("user@example.invalid");
    login.findChild<QLineEdit *>("pass_edit")->setText("Password123");
    login.findChild<QPushButton *>("login_btn")->click();
    QTRY_COMPARE(UserMgr::GetInstance()->GetUid(), 101);
    auto user = std::make_shared<UserInfo>(102, "砂糖白柚", ":/res/head_1.jpg");
    UserMgr::GetInstance()->AddFriend(user);
    ApplyFriend edit;
    edit.SetContactInfo(user);
    edit.findChild<QLineEdit *>("back_ed")->setText("李同学");
    edit.findChild<QPushButton *>("sure_btn")->click();
    QCOMPARE(user->_back, QString());
    QTRY_COMPARE(edit.result(), int(QDialog::Accepted));
    QCOMPARE(user->_back, QString("李同学"));
    QCOMPARE(peer.chatRequests.back()["friend_uid"].toInt(), 102);
    ContactUserList contacts;
    contacts.Reload();
    QVERIFY(!contacts.findItems("李同学", Qt::MatchExactly).isEmpty());
  }
  void featureScreenshots()
  {
    const auto artifacts = qEnvironmentVariable("CPPCHAT_TEST_ARTIFACTS");
    if (artifacts.isEmpty())
      return;
    QDir().mkpath(artifacts);
    QString error;
    QVERIFY2(ThemeManager::apply(*qApp, &error), qPrintable(error));
    auto user = std::make_shared<UserInfo>(102, "砂糖白柚", ":/res/head_1.jpg");
    UserMgr::GetInstance()->AddFriend(user);
    ContactUserList contacts;
    contacts.resize(300, 600);
    contacts.Reload();
    contacts.show();
    LoginDialog login;
    login.show();
    ApplyFriend edit;
    edit.SetContactInfo(user);
    edit.show();
    ClientUpdateService service(QUrl("https://updates.example.test/latest.json"), "0.1.7");
    UpdateDialog updates(&service);
    updates.show();
    QTest::qWait(30);
    QVERIFY(contacts.grab().save(artifacts + "/contacts-pinyin.png"));
    QVERIFY(login.grab().save(artifacts + "/automatic-login.png"));
    QVERIFY(edit.grab().save(artifacts + "/edit-remark.png"));
    QVERIFY(updates.grab().save(artifacts + "/update-history.png"));
  }
#ifdef Q_OS_WIN
  void windowLogoutClearsCredentialsAndDoesNotStartAnotherAutomaticAttempt()
  {
    if (!AutoLoginStore::supported())
      QSKIP("Secure automatic login currently requires Windows DPAPI");
    LoginPeer peer;
    gate_url_prefix = peer.url();
    AutoLoginStore::save(QUrl(peer.url()), {"user@example.invalid", "Password123"});
    MainWindow window;
    QTRY_COMPARE(UserMgr::GetInstance()->GetUid(), 101);
    auto *chat = window.findChild<ChatDialog *>();
    QVERIFY(chat);
    QVERIFY(QMetaObject::invokeMethod(chat, "sig_logout", Qt::DirectConnection));
    QVERIFY(!AutoLoginStore::load(QUrl(peer.url())));
    QTest::qWait(30);
    QCOMPARE(peer.httpRequests.size(), 1);
  }
#else
  void unsupportedPlatformsDoNotPersistPasswords()
  {
    const QUrl server("https://one.example.test");
    QVERIFY(!AutoLoginStore::supported());
    QVERIFY(!AutoLoginStore::load(server));
    QVERIFY_EXCEPTION_THROWN(AutoLoginStore::save(server, {"user@example.invalid", "Password123"}),
                             std::exception);
    LoginDialog login;
    auto *option = login.findChild<QCheckBox *>("auto_login_check");
    QVERIFY(option && !option->isEnabled() && !option->isChecked());
  }
#endif
};
QTEST_MAIN(ClientFeatureTests)
#include "client_features_tests.moc"
