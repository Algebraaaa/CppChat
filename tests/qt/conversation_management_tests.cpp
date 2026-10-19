#include "app/thememanager.h"
#include "chatstorefixture.h"
#include "common/conversationpreferences.h"
#include "common/runtimepaths.h"
#include "dialogs/chatdialog.h"
#include "dialogs/chatpage.h"
#include "dialogs/conversationmenu.h"
#include "dialogs/conversationsection.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/chatsyncservice.h"
#include "widgets/MessageTextEdit.h"
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using QtFixtures::awaitStore;
class ConversationManagementTests : public QObject {
  Q_OBJECT
  QTemporaryDir root;
  int uid = 200;
  ChatStore *store = ChatStore::instance();
  std::shared_ptr<ChatThreadData> data;
  const QString server = "https://conversations.example.test";
  StoredMessage message(int id, const QString &text = "confirmed")
  {
    StoredMessage value;
    value.threadId = 7;
    value.senderUid = 102;
    value.serverId = id;
    value.uniqueId = QString("message-%1").arg(id);
    value.content = text;
    value.time = "2026-10-19 10:00:00";
    return value;
  }
  void populate(ConversationSection &section)
  {
    section.LoadLocalState();
    section.resize(1000, 700);
    section.AddThreads({}, 10);
    section.SelectThread(data);
    section.show();
  }
  void choose(bool remove, const std::function<void()> &before = {})
  {
    QTimer::singleShot(0, this, [remove, before] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      QVERIFY(box);
      QCOMPARE(box->objectName(), QString("deleteLocalConversationConfirm"));
      QCOMPARE(box->defaultButton()->text(), QString("取消"));
      if (before)
        before();
      for (auto *button : box->buttons())
        if (button->text() == (remove ? "删除本机记录并隐藏" : "取消")) {
          button->click();
          return;
        }
      QFAIL("Confirmation choice missing");
    });
  }
private slots:
  void initTestCase()
  {
    QVERIFY(root.isValid());
    RuntimePaths::configure(root.filePath("config.ini"), root.path(), root.filePath("logs"));
  }
  void init()
  {
    ++uid;
    gate_url_prefix = server;
    TcpMgr::GetInstance()->CloseConnection();
    auto mgr = UserMgr::GetInstance();
    mgr->Reset();
    mgr->SetUserInfo(std::make_shared<UserInfo>(uid, "self", ""));
    mgr->AddFriend(std::make_shared<UserInfo>(102, "同事", ""));
    data = std::make_shared<ChatThreadData>(102, 7, 0);
    mgr->AddChatThreadData(data, 102);
    auto result = awaitStore([&](auto done) { store->OpenAccount(uid, server, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    result = awaitStore([&](auto done) { store->SaveConversations({{7, 102}}, done); });
    QVERIFY(result.error.isEmpty());
    const auto record = message(3);
    result = awaitStore([&](auto done) { store->SaveMessages({record}, 0, -1, done); });
    QVERIFY(result.error.isEmpty());
    data->MoveMsg(record.ToMessage());
    QString error;
    QVERIFY2(ThemeManager::apply(*qApp, &error), qPrintable(error));
  }
  void cleanup()
  {
    ConversationPreferences::Flush();
    store->CloseAccount();
    UserMgr::GetInstance()->Reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void cleanupTestCase() { store->Shutdown(); }
  void scopedPreferencesSurviveReloadAndRejectInvalidBoundaries()
  {
    QVERIFY(ConversationPreferences::SaveMuted(uid, {7}));
    QVERIFY(ConversationPreferences::SaveHidden(uid, {{7, 3}, {-1, 5}, {8, -2}}));
    auto state = ConversationPreferences::Load(uid);
    QCOMPARE(state.muted, QSet<int>{7});
    QCOMPARE(state.hidden, (QHash<int, int>{{7, 3}}));
    QVERIFY(ConversationPreferences::Load(uid + 1).muted.isEmpty());
    gate_url_prefix = "https://other.example.test";
    QVERIFY(ConversationPreferences::Load(uid).muted.isEmpty());
    QVERIFY(ConversationPreferences::Load(uid).hidden.isEmpty());
    gate_url_prefix = server;
    ConversationPreferences::RemoveThread(uid, 7);
    QVERIFY(ConversationPreferences::Load(uid).muted.isEmpty());
    QVERIFY(ConversationPreferences::Load(uid).hidden.isEmpty());
  }
  void muteMenuAndPanelAgreeAndSuppressOnlyAlerts()
  {
    ConversationSection section;
    populate(section);
    QSignalSpy network(TcpMgr::GetInstance().get(), &TcpMgr::sig_send_data);
    auto *menu = section.findChild<ConversationMenu *>();
    menu->Popup(7, false, true, QPoint(20, 20));
    menu->findChild<QPushButton *>("mute_btn")->click();
    QVERIFY(section.IsMuted(7));
    QVERIFY(!section.HasAlert({7}));
    QVERIFY(section.HasAlert({7, 8}));
    auto *toggle = section.findChild<QCheckBox *>("mute_switch");
    QVERIFY(toggle->isChecked());
    section.UpdateThread(data, true);
    auto *list = section.findChild<QListWidget *>("chat_user_list");
    auto *row = list->itemWidget(list->item(0));
    QVERIFY(row->findChild<QLabel *>("unread_dot")->isHidden());
    QVERIFY(!row->findChild<QLabel *>("conversationMuteIcon")->isHidden());
    const auto artifacts = qEnvironmentVariable("CPPCHAT_TEST_ARTIFACTS");
    if (!artifacts.isEmpty()) {
      QVERIFY(QDir().mkpath(artifacts));
      QVERIFY(section.grab().save(artifacts + "/conversation-muted.png"));
    }
    toggle->setChecked(false);
    QVERIFY(!section.IsMuted(7));
    QVERIFY(!row->findChild<QLabel *>("unread_dot")->isHidden());
    QCOMPARE(data->GetMsgMapRef().size(), 1);
    QCOMPARE(network.size(), 0);
  }
  void globalUnreadIndicatorFollowsMuteWithoutLosingUnreadState()
  {
    ChatDialog dialog;
    dialog.StartSession();
    QTRY_VERIFY(dialog.findChild<ChatSyncService *>()->IsReady());
    auto *section = dialog.findChild<ConversationSection *>();
    auto *dot = dialog.findChild<QLabel *>("chat_unread_dot");
    section->SetThreadMuted(7, true);
    dialog.findChild<ChatSyncService *>()->incomingMessage(7);
    QVERIFY(dot->isHidden());
    QVERIFY(ConversationPreferences::Load(uid).unread.contains(7));
    section->SetThreadMuted(7, false);
    QVERIFY(!dot->isHidden());
    dialog.ResetSession();
  }
  void hidingPreservesDraftAndHistoryAndOnlyNewMessagesReveal()
  {
    ConversationSection section;
    populate(section);
    auto *edit = section.findChild<MessageTextEdit *>();
    QTRY_VERIFY(edit->isEnabled());
    edit->setPlainText("保留的草稿");
    QSignalSpy network(TcpMgr::GetInstance().get(), &TcpMgr::sig_send_data);
    QSignalSpy hidden(&section, &ConversationSection::sig_thread_hidden);
    auto *menu = section.findChild<ConversationMenu *>();
    menu->Popup(7, false, false, QPoint(20, 20));
    menu->findChild<QPushButton *>("hide_btn")->click();
    QVERIFY(section.IsHidden(7));
    QCOMPARE(section.ThreadCount(), 0);
    QCOMPARE(hidden.size(), 1);
    section.UpdateThread(data, true);
    section.AddThreads({}, 10);
    QCOMPARE(section.ThreadCount(), 0);
    auto result = awaitStore([&](auto done) { store->LoadMessages(7, 0, 10, done); });
    QCOMPARE(result.messages.size(), 1);
    result = awaitStore([&](auto done) { store->LoadDraft(7, done); });
    QCOMPARE(result.draft.first().content, QString("保留的草稿"));
    ConversationSection reopened;
    reopened.LoadLocalState();
    reopened.AddThreads({}, 10);
    QCOMPARE(reopened.ThreadCount(), 0);
    data->MoveMsg(message(4, "新消息").ToMessage());
    reopened.UpdateThread(data, true);
    QCOMPARE(reopened.ThreadCount(), 1);
    QVERIFY(!reopened.IsHidden(7));
    reopened.HideThread(7);
    QVERIFY(reopened.RevealThread(7));
    reopened.UpdateThread(data, false);
    reopened.SelectThread(data);
    QTRY_VERIFY(reopened.findChild<MessageTextEdit *>()->isEnabled());
    QCOMPARE(reopened.findChild<MessageTextEdit *>()->toPlainText(), QString("保留的草稿"));
    QCOMPARE(network.size(), 0);
  }
  void deletionRequiresConfirmationAndSuppressesOldHistoryAfterReopen()
  {
    ConversationSection section;
    populate(section);
    QSignalSpy network(TcpMgr::GetInstance().get(), &TcpMgr::sig_send_data);
    choose(false);
    section.DeleteLocalThread(7);
    QCOMPARE(section.ThreadCount(), 1);
    choose(true);
    section.DeleteLocalThread(7);
    QTRY_VERIFY(section.IsHidden(7));
    QCOMPARE(section.ThreadCount(), 0);
    QVERIFY(data->GetMsgMapRef().isEmpty());
    QVERIFY(UserMgr::GetInstance()->CheckFriendById(102));
    QVERIFY(UserMgr::GetInstance()->GetChatThreadByThreadId(7));
    auto result = awaitStore([&](auto done) { store->SaveMessages({message(3)}, 7, 3, done); });
    QVERIFY(result.suppressedMessageKeys.contains("7:3"));
    store->CloseAccount();
    result = awaitStore([&](auto done) { store->OpenAccount(uid, server, done); });
    QVERIFY(result.error.isEmpty());
    result = awaitStore([&](auto done) { store->LoadMessages(7, 0, 10, done); });
    QVERIFY(result.messages.isEmpty());
    QCOMPARE(network.size(), 0);
  }
  void pendingArrivalDuringConfirmationRollsBackDeletion()
  {
    ConversationSection section;
    populate(section);
    choose(true, [&] {
      auto pending = message(0, "pending");
      pending.senderUid = uid;
      pending.uniqueId = "pending-during-confirmation";
      const auto result =
          awaitStore([&](auto done) { store->SaveMessages({pending}, 0, -1, done); });
      QVERIFY(result.error.isEmpty());
    });
    QSignalSpy error(store, &ChatStore::storageError);
    section.DeleteLocalThread(7);
    QTRY_VERIFY(error.size() > 0);
    QVERIFY(!section.IsHidden(7));
    QCOMPARE(section.ThreadCount(), 1);
    const auto result = awaitStore([&](auto done) { store->LoadMessages(7, 0, 10, done); });
    QCOMPARE(result.messages.size(), 2);
  }
  void failedPreferenceWriteDoesNotHideOrMuteConversation()
  {
    ConversationSection section;
    populate(section);
    QVERIFY(ConversationPreferences::SaveMuted(uid, {}));
    const QString path = RuntimePaths::preferencesDirectory() + "/conversations.ini";
    QVERIFY(QFile::rename(path, path + ".backup"));
    QVERIFY(QDir().mkdir(path));
    section.SetThreadMuted(7, true);
    section.HideThread(7);
    QVERIFY(!section.IsMuted(7));
    QVERIFY(!section.IsHidden(7));
    QCOMPARE(section.ThreadCount(), 1);
    QVERIFY(QDir().rmdir(path));
    QVERIFY(QFile::rename(path + ".backup", path));
    QVERIFY(!ConversationPreferences::Load(uid).muted.contains(7));
    QVERIFY(!ConversationPreferences::Load(uid).hidden.contains(7));
  }
  void groupDeletionPreservesMembershipAndAccountChangeInvalidatesConfirmation()
  {
    data->SetOtherId(0);
    data->SetGroup(QJsonObject{{"thread_id", 7}, {"name", "测试群"}, {"owner_uid", uid}});
    ConversationSection section;
    populate(section);
    QSignalSpy network(TcpMgr::GetInstance().get(), &TcpMgr::sig_send_data);
    choose(true, [&] {
      UserMgr::GetInstance()->SetUserInfo(std::make_shared<UserInfo>(uid + 1, "other", ""));
    });
    section.DeleteLocalThread(7);
    QCOMPARE(section.ThreadCount(), 1);
    UserMgr::GetInstance()->SetUserInfo(std::make_shared<UserInfo>(uid, "self", ""));
    choose(true);
    section.DeleteLocalThread(7);
    QTRY_VERIFY(section.IsHidden(7));
    QVERIFY(data->IsGroup());
    QCOMPARE(data->Group()["owner_uid"].toInt(), uid);
    QCOMPARE(UserMgr::GetInstance()->GetChatThreadByThreadId(7), data);
    QCOMPARE(network.size(), 0);
  }
  void clearedHistoryWithFailedHideKeepsVisibleConversationAndFreshPreview()
  {
    ConversationSection section;
    populate(section);
    QVERIFY(ConversationPreferences::SaveHidden(uid, {}));
    const QString path = RuntimePaths::preferencesDirectory() + "/conversations.ini";
    QVERIFY(QFile::rename(path, path + ".backup"));
    QVERIFY(QDir().mkdir(path));
    choose(true);
    section.DeleteLocalThread(7);
    QTRY_VERIFY(data->GetMsgMapRef().isEmpty());
    QVERIFY(!section.IsHidden(7));
    QCOMPARE(section.ThreadCount(), 1);
    auto *list = section.findChild<QListWidget *>("chat_user_list");
    QVERIFY(!list->itemWidget(list->item(0))
                 ->findChild<QLabel *>("user_chat_lb")
                 ->text()
                 .contains("confirmed"));
    auto result = awaitStore([&](auto done) { store->LoadMessages(7, 0, 10, done); });
    QVERIFY(result.messages.isEmpty());
    QVERIFY(QDir().rmdir(path));
    QVERIFY(QFile::rename(path + ".backup", path));
  }
};
QTEST_MAIN(ConversationManagementTests)
#include "conversation_management_tests.moc"
