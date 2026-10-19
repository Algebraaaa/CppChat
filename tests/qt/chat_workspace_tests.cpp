#include "app/thememanager.h"
#include "chatstorefixture.h"
#include "common/conversationpreferences.h"
#include "common/runtimepaths.h"
#include "dialogs/chatdialog.h"
#include "dialogs/chathistorydialog.h"
#include "dialogs/chatpage.h"
#include "dialogs/chatwindow.h"
#include "dialogs/conversationmenu.h"
#include "dialogs/conversationsection.h"
#include "dialogs/sidemenupopup.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/chatsyncservice.h"
#include "services/messagesender.h"
#include "widgets/MessageTextEdit.h"
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>

using QtFixtures::awaitStore;

class ChatWorkspaceTests : public QObject {
  Q_OBJECT
  QTemporaryDir root;
  ChatStore *store = ChatStore::instance();
  int uid = 500;
  const QString server = "https://workspace.example.test";
  std::shared_ptr<ChatThreadData> first, second;
  StoredMessage message(int thread, int id, const QString &text = "正文 100%_", int type = 0,
                        const QString &time = "2026-10-19 10:00:00")
  {
    StoredMessage m;
    m.threadId = thread;
    m.serverId = id;
    m.senderUid = 102;
    m.uniqueId = QString("workspace-%1-%2").arg(thread).arg(id);
    m.content = text;
    m.type = type;
    m.time = time;
    return m;
  }
  ChatStoreResult search(int thread = 0, const QString &text = {}, int type = -1,
                         HistoryCursor cursor = {}, const QString &date = {},
                         const QVector<int> &allowed = {7, 8})
  {
    return awaitStore(
        [&](auto done) { store->SearchMessages(thread, text, type, cursor, done, date, allowed); });
  }
  void save(const QVector<StoredMessage> &messages)
  {
    const auto result = awaitStore([&](auto done) { store->SaveMessages(messages, 0, -1, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
  }
  void snapshot(QWidget &widget, const QString &name)
  {
    const auto directory = qEnvironmentVariable("CPPCHAT_TEST_ARTIFACTS");
    if (directory.isEmpty())
      return;
    QTest::qWait(80); // Let deferred text sizing and widget layout settle before capture.
    QVERIFY(QDir().mkpath(directory));
    QVERIFY(widget.grab().save(directory + '/' + name + ".png"));
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
    const auto mgr = UserMgr::GetInstance();
    mgr->Reset();
    mgr->SetUserInfo(std::make_shared<UserInfo>(uid, "self", ""));
    auto user = std::make_shared<UserInfo>(102, "Alice", "");
    user->_back = "同事";
    mgr->AddFriend(user);
    mgr->AddFriend(std::make_shared<UserInfo>(103, "Bob", ""));
    first = std::make_shared<ChatThreadData>(102, 7, 0);
    second = std::make_shared<ChatThreadData>(103, 8, 0);
    mgr->AddChatThreadData(first, 102);
    mgr->AddChatThreadData(second, 103);
    auto result = awaitStore([&](auto done) { store->OpenAccount(uid, server, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    result = awaitStore(
        [&](auto done) { store->SaveConversations({{7, 102}, {8, 103}, {99, 199}}, done); });
    QVERIFY(result.error.isEmpty());
    QString error;
    QVERIFY2(ThemeManager::apply(*qApp, &error), qPrintable(error));
  }
  void cleanup()
  {
    store->CloseAccount();
    UserMgr::GetInstance()->Reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void cleanupTestCase() { store->Shutdown(); }

  void globalSearchUsesScopedChronologicalKeysetAndLiteralFilters()
  {
    QVector<StoredMessage> records;
    for (int i = 1; i <= 55; ++i) {
      records.append(message(7, i));
      records.append(message(8, i));
    }
    records.append(message(99, 9999, "不可访问", 0, "2026-10-20 10:00:00"));
    records.append(message(7, 56, "最新", 0, "2026-10-20 11:00:00"));
    save(records);
    auto result = search();
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.messages.size(), 50);
    QVERIFY(result.more);
    QCOMPARE(result.messages.first().content, QString("最新"));
    QSet<qint64> ids;
    for (;;) {
      for (const auto &m : result.messages) {
        QVERIFY(m.threadId == 7 || m.threadId == 8);
        QVERIFY(!ids.contains(m.localId));
        ids.insert(m.localId);
      }
      if (!result.more)
        break;
      result = search(0, {}, -1, result.nextCursor);
      QVERIFY(result.error.isEmpty());
    }
    QCOMPARE(ids.size(), 111);
    result = search(0, "%_", -1, {}, "2026-10-19");
    QCOMPARE(result.messages.size(), 50);
    result = search(0, "", -1, {}, {}, {});
    QVERIFY(result.messages.isEmpty());
    QVERIFY(!search(0, {}, 80).error.isEmpty());
    QVERIFY(!search(0, {}, -1, {}, "invalid").error.isEmpty());
    result = search(7);
    QCOMPARE(result.messages.first().serverId, 56);
    for (const auto &m : result.messages)
      QCOMPARE(m.threadId, 7);
  }

  void localDeletionBoundariesAndCategoriesRemainEffective()
  {
    save({message(7, 1, "删除"), message(7, 2, "https://example.test"),
          message(8, 1, "受保护图片", 1), message(8, 2, "语音", 3)});
    QCOMPARE(search(0, {}, 3).messages.size(), 1);
    QCOMPARE(search(0, {}, 4).messages.first().type, 3);
    QCOMPARE(search(0, {}, 1).messages.first().threadId, 8);
    auto result = awaitStore([&](auto done) { store->DeleteLocalMessage(7, 1, done); });
    QVERIFY(result.error.isEmpty());
    QCOMPARE(search().messages.size(), 3);
    result = awaitStore([&](auto done) { store->ClearLocalHistory(8, done); });
    QVERIFY(result.error.isEmpty());
    QCOMPARE(search().messages.size(), 1);
    save({message(8, 1, "旧记录"), message(8, 2, "旧语音", 3)});
    QCOMPARE(search().messages.size(), 1);
  }

  void independentQueriesDoNotCancelEachOther()
  {
    save({message(7, 1, "first"), message(8, 1, "second")});
    bool privateDone = false, globalDone = false;
    ChatStoreResult privateResult, globalResult;
    store->SearchMessages(7, "first", -1, {}, [&](auto result) {
      privateResult = result;
      privateDone = true;
    });
    store->SearchMessages(0, "second", -1, {},
                          [&](auto result) {
                            globalResult = result;
                            globalDone = true;
                          },
                          {}, {7, 8});
    QTRY_VERIFY(privateDone && globalDone);
    QCOMPARE(privateResult.messages.size(), 1);
    QCOMPARE(globalResult.messages.size(), 1);
    QCOMPARE(privateResult.messages.first().threadId, 7);
    QCOMPARE(globalResult.messages.first().threadId, 8);
  }

  void globalHistoryFiltersSourceNavigationAndAccountReset()
  {
    save({message(7, 1, "first"), message(8, 1, "second")});
    ChatHistoryDialog dialog;
    dialog.SetAllConversations();
    dialog.show();
    auto *list = dialog.findChild<QListWidget *>("results_list");
    auto *scope = dialog.findChild<QComboBox *>("historyConversationFilter");
    auto *query = dialog.findChild<QLineEdit *>("search_edit");
    QTRY_COMPARE(list->count(), 2);
    QCOMPARE(scope->count(), 3);
    scope->setCurrentIndex(scope->findData(7));
    QTRY_COMPARE(list->count(), 1);
    QSignalSpy opened(&dialog, &ChatHistoryDialog::openThreadRequested);
    auto *button =
        list->itemWidget(list->item(0))->findChild<QPushButton *>("openHistoryConversationButton");
    QVERIFY(button && button->text().contains("同事"));
    button->click();
    QCOMPARE(opened.takeFirst().first().toInt(), 7);
    scope->setCurrentIndex(0);
    query->setText("second");
    QCOMPARE(list->count(), 0); // Old results cannot be clicked during debounce.
    QTRY_COMPARE(list->count(), 1);
    button =
        list->itemWidget(list->item(0))->findChild<QPushButton *>("openHistoryConversationButton");
    QCOMPARE(button->property("threadId").toInt(), 8);
    UserMgr::GetInstance()->RemoveFriend(103);
    emit TcpMgr::GetInstance() -> sig_friend_deleted(103);
    QTRY_COMPARE(list->count(), 0);
    query->clear();
    QTRY_COMPARE(list->count(), 1);
    store->CloseAccount();
    QVERIFY(!dialog.isVisible());
    QCOMPARE(list->count(), 0);
    QVERIFY(query->text().isEmpty());
  }

  void twoHistoryWindowsStayIndependentAndDarkPaletteReadable()
  {
    save({message(7, 1, "第一段"), message(8, 1, "第二段")});
    QPalette previous = qApp->palette();
    QPalette dark = previous;
    dark.setColor(QPalette::WindowText, Qt::white);
    dark.setColor(QPalette::Text, Qt::white);
    dark.setColor(QPalette::Base, QColor("#202020"));
    qApp->setPalette(dark);
    ChatHistoryDialog global, single;
    global.SetAllConversations();
    single.SetConversation(7, "同事");
    global.show();
    single.show();
    auto *all = global.findChild<QListWidget *>("results_list");
    auto *one = single.findChild<QListWidget *>("results_list");
    QTRY_COMPARE(all->count(), 2);
    QTRY_COMPARE(one->count(), 1);
    auto *label = all->itemWidget(all->item(0))->findChild<QLabel *>();
    QVERIFY(label);
    label->ensurePolished();
    QCOMPARE(label->palette().color(QPalette::WindowText), QColor("#303236"));
    QTRY_VERIFY(
        all->itemWidget(all->item(0))->findChild<QTextEdit *>("chat_message_text")->isVisible());
    snapshot(global, "global-history");
    global.findChild<QLineEdit *>("search_edit")->setText("第二段");
    QTRY_COMPARE(all->count(), 1);
    QCOMPARE(one->count(), 1);
    qApp->setPalette(previous);
  }

  void staleAccessSnapshotAndGroupRemovalCannotRestoreOldResults()
  {
    auto group = std::make_shared<ChatThreadData>(0, 9, 0);
    group->SetGroup({{"name", "开发群"}, {"owner_uid", uid}, {"thread_id", 9}});
    UserMgr::GetInstance()->AddChatThreadData(group, 0);
    auto result = awaitStore([&](auto done) { store->SaveConversations({{9, 0}}, done); });
    QVERIFY(result.error.isEmpty());
    save({message(7, 1, "好友记录"), message(9, 1, "群记录"), message(99, 1, "残留记录")});
    ChatHistoryDialog dialog;
    dialog.SetAllConversations();
    UserMgr::GetInstance()->RemoveFriend(102); // While the first query is in flight.
    dialog.show();
    auto *list = dialog.findChild<QListWidget *>("results_list");
    QTRY_COMPARE(list->count(), 1);
    auto *button =
        list->itemWidget(list->item(0))->findChild<QPushButton *>("openHistoryConversationButton");
    QVERIFY(button && button->text().contains("开发群"));
    UserMgr::GetInstance()->RemoveGroup(9);
    emit TcpMgr::GetInstance() -> sig_group_info({{"thread_id", 9}, {"removed", true}});
    QCOMPARE(list->count(), 0);
    QTRY_COMPARE(dialog.findChild<QComboBox *>("historyConversationFilter")->count(), 2);
    dialog.findChild<QPushButton *>("refresh_btn")->click();
    QTRY_VERIFY(dialog.findChild<QLabel *>("empty_title")->text().contains("没有找到"));
    QCOMPARE(list->count(), 0);
  }

  void clearingHistoryRefreshesBothOpenViews()
  {
    save({message(7, 1)});
    ChatHistoryDialog global, single;
    global.SetAllConversations();
    single.SetConversation(7, "同事");
    global.show();
    single.show();
    auto *all = global.findChild<QListWidget *>("results_list");
    auto *one = single.findChild<QListWidget *>("results_list");
    QTRY_COMPARE(all->count(), 1);
    QTRY_COMPARE(one->count(), 1);
    const auto result = awaitStore([&](auto done) { store->ClearLocalHistory(7, done); });
    QVERIFY(result.error.isEmpty());
    QTRY_COMPARE(all->count(), 0);
    QTRY_COMPARE(one->count(), 0);
  }

  void detachReusesEditorSenderAndDraftThroughCloseAndSwitch()
  {
    ConversationSection section;
    section.resize(1100, 750);
    section.LoadLocalState();
    section.AddThreads({}, 10);
    section.SelectThread(first);
    section.show();
    auto *page = section.Chat();
    auto *editor = page->findChild<MessageTextEdit *>("chatEdit");
    QTRY_VERIFY(editor->isEnabled());
    editor->insertPlainText("独立窗口草稿");
    auto *sender = page->findChild<MessageSender *>();
    auto *splitter = section.findChild<QSplitter *>("main_splitter");
    section.OpenChatWindow();
    auto *window = section.findChild<ChatWindow *>();
    QVERIFY(window && window->isVisible());
    QCOMPARE(page->window(), window);
    QCOMPARE(section.Chat(), page);
    QCOMPARE(section.findChildren<MessageSender *>().size(), 1);
    QCOMPARE(page->findChild<MessageSender *>(), sender);
    QCOMPARE(editor->toPlainText(), QString("独立窗口草稿"));
    QVERIFY(window->windowTitle().contains("同事"));
    section.OpenChatWindow();
    QCOMPARE(section.findChildren<ChatWindow *>().size(), 1);
    snapshot(*window, "detached-chat");
    section.SelectThread(second);
    QTRY_VERIFY(editor->isEnabled());
    editor->insertPlainText("另一段草稿");
    QVERIFY(window->windowTitle().contains("Bob"));
    window->close();
    QVERIFY(!window->isVisible());
    QCOMPARE(splitter->widget(1), page);
    QCOMPARE(editor->toPlainText(), QString("另一段草稿"));
    section.SelectThread(first);
    QCOMPARE(editor->toPlainText(), QString("独立窗口草稿"));
    section.OpenChatWindow();
    QTest::keyClick(window, Qt::Key_Escape);
    QVERIFY(!window->isVisible());
    QCOMPARE(splitter->widget(1), page);
    auto result = awaitStore([&](auto done) { store->LoadDraft(7, done); });
    QCOMPARE(result.draft.first().content, QString("独立窗口草稿"));
    section.OpenChatWindow();
    store->CloseAccount();
    QVERIFY(!window->isVisible());
    QCOMPARE(splitter->widget(1), page);
    QVERIFY(editor->toPlainText().isEmpty());
    QVERIFY(!editor->isEnabled());
  }

  void actualMenusOpenHistoryAndDetachWithoutPlaceholder()
  {
    save({message(7, 1, "entry")});
    ChatDialog dialog;
    dialog.show();
    auto *section = dialog.findChild<ConversationSection *>();
    section->LoadLocalState();
    section->AddThreads({}, 10);
    emit section->sig_open_thread(7);
    auto *menu = section->findChild<ConversationMenu *>();
    menu->Popup(7, false, false, QPoint(20, 20));
    menu->findChild<QPushButton *>("window_btn")->click();
    auto *window = section->findChild<ChatWindow *>();
    QVERIFY(window && window->isVisible());
    snapshot(*window, "actual-detached-chat");
    dialog.findChild<QPushButton *>("side_contact_btn")->click();
    window->activateWindow();
    QTRY_VERIFY(window->isActiveWindow());
    auto *sync = dialog.findChild<ChatSyncService *>();
    sync->incomingMessage(7);
    QVERIFY(!ConversationPreferences::Load(uid).unread.contains(7));
    dialog.activateWindow();
    QTRY_VERIFY(dialog.isActiveWindow());
    sync->incomingMessage(7);
    QVERIFY(ConversationPreferences::Load(uid).unread.contains(7));
    window->activateWindow();
    QTRY_VERIFY(!ConversationPreferences::Load(uid).unread.contains(7));
    dialog.findChild<QPushButton *>("side_menu_btn")->click();
    auto *side = dialog.findChild<SideMenuPopup *>();
    side->findChild<QPushButton *>("chat_history_btn")->click();
    auto *history = dialog.findChild<ChatHistoryDialog *>();
    QVERIFY(history && history->isVisible());
    auto *list = history->findChild<QListWidget *>("results_list");
    QTRY_COMPARE(list->count(), 1);
    auto *query = history->findChild<QLineEdit *>("search_edit");
    query->setText("discarded query");
    query->clear();
    QTRY_COMPARE(list->count(), 1);
    list->itemWidget(list->item(0))
        ->findChild<QPushButton *>("openHistoryConversationButton")
        ->click();
    QCOMPARE(section->Chat()->window(), window);
    QVERIFY(window->isVisible());
    dialog.ResetSession();
    QVERIFY(!window->isVisible());
    QVERIFY(!history->isVisible());
  }
};

QTEST_MAIN(ChatWorkspaceTests)
#include "chat_workspace_tests.moc"
