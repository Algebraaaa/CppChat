#include "app/thememanager.h"
#include "chatstorefixture.h"
#include "common/localstoresecurity.h"
#include "common/runtimepaths.h"
#include "dialogs/chathistorydialog.h"
#include "dialogs/chatpage.h"
#include "dialogs/messageactions.h"
#include "dialogs/messagemenu.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/chatsyncservice.h"
#include "widgets/MessageTextEdit.h"
#include "widgets/chatitembase.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>

using QtFixtures::awaitStore;

class MessageActionsTests : public QObject {
  Q_OBJECT
  QTemporaryDir root;
  int fixtureIndex = 0;
  ChatStore *store = ChatStore::instance();
  const QString server = "https://messages.example.test";
  std::shared_ptr<ChatThreadData> data;

  StoredMessage record(int id, const QString &text, int thread = 7) const
  {
    StoredMessage result;
    result.threadId = thread;
    result.senderUid = 102;
    result.serverId = id;
    result.uniqueId = "message-" + QString::number(id);
    result.content = text;
    result.time = "2026-10-18 12:00:00";
    return result;
  }
  void seed(const QVector<StoredMessage> &records)
  {
    const auto result = awaitStore([&](auto done) { store->SaveMessages(records, 0, -1, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    for (const auto &stored : records)
      if (stored.threadId == 7)
        data->MoveMsg(stored.ToMessage());
  }
  MessageMenu *menu(ChatPage &page, const QString &content)
  {
    for (auto *item : page.findChildren<ChatItemBase *>())
      if (item->property("message_content").toString() == content) {
        item->messageContextMenuRequested(page.mapToGlobal(QPoint(40, 40)));
        return item->findChild<MessageMenu *>();
      }
    return nullptr;
  }
  void chooseDeletion(bool remove)
  {
    QTimer::singleShot(0, this, [remove] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      QVERIFY(box);
      QCOMPARE(box->defaultButton()->text(), QString("取消"));
      for (auto *button : box->buttons())
        if (button->text() == (remove ? "仅从本机删除" : "取消")) {
          button->click();
          return;
        }
      QFAIL("Deletion choice missing");
    });
  }
  void sql(const QString &statement)
  {
    const QString name = "message-action-fixture";
    {
      auto database = QSqlDatabase::addDatabase(LocalStoreSecurity::driverName(), name);
      database.setDatabaseName(store->AccountDirectory() + "/sql/db.sqlite");
      LocalStoreSecurity::configureDatabase(
          database, LocalStoreSecurity::accountKey(store->AccountDirectory()));
      QVERIFY(database.open());
      QSqlQuery query(database);
      QVERIFY2(query.exec(statement), qPrintable(statement));
    }
    QSqlDatabase::removeDatabase(name);
  }
private slots:
  void init()
  {
    QVERIFY(root.isValid());
    const QString directory = root.filePath(QString::number(++fixtureIndex));
    QVERIFY(QDir().mkpath(directory));
    RuntimePaths::configure(directory + "/config.ini", directory, directory + "/logs");
    gate_url_prefix = server;
    TcpMgr::GetInstance()->CloseConnection();
    auto mgr = UserMgr::GetInstance();
    mgr->Reset();
    mgr->SetUserInfo(std::make_shared<UserInfo>(101, "self", ""));
    auto friendInfo = std::make_shared<UserInfo>(102, "friend", "");
    friendInfo->_back = "同事";
    mgr->AddFriend(friendInfo);
    mgr->AddFriend(std::make_shared<UserInfo>(103, "other", ""));
    data = std::make_shared<ChatThreadData>(102, 7, 0);
    mgr->AddChatThreadData(data, 102);
    auto result = awaitStore([&](auto done) { store->OpenAccount(101, server, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    result = awaitStore([&](auto done) { store->SaveConversations({{7, 102}, {8, 103}}, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
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

  void deletionSurvivesHistoryReplaySearchRestartAndAccountSwitch()
  {
    seed({record(1, "keep"), record(2, "remove"), record(2, "other thread", 8)});
    QSignalSpy deleted(store, &ChatStore::localMessageDeleted);
    auto result = awaitStore([&](auto done) { store->DeleteLocalMessage(7, 2, done); });
    QVERIFY(result.error.isEmpty());
    QCOMPARE(deleted.size(), 1);
    result = awaitStore([&](auto done) { store->SearchMessages(7, "remove", -1, {}, done); });
    QVERIFY(result.messages.isEmpty());
    result = awaitStore([&](auto done) {
      store->SaveMessages({record(1, "keep"), record(2, "remove"), record(3, "new")}, 7, 3, done);
    });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QVERIFY(result.suppressedMessageKeys.contains("7:2"));
    QVERIFY(!result.newMessageKeys.contains("7:2"));
    store->CloseAccount();
    result = awaitStore([&](auto done) { store->OpenAccount(101, server, done); });
    QCOMPARE(result.cursors.value(7), 3);
    result = awaitStore([&](auto done) { store->LoadMessages(7, 0, 10, done); });
    QCOMPARE(result.messages.size(), 2);
    QCOMPARE(result.messages.last().content, QString("new"));
    result = awaitStore([&](auto done) { store->LoadMessages(8, 0, 10, done); });
    QCOMPARE(result.messages.size(), 1);
    QCOMPARE(result.messages.first().content, QString("other thread"));
    result = awaitStore([&](auto done) { store->DeleteLocalMessage(7, 3, done); });
    QVERIFY(result.error.isEmpty());
    result = awaitStore([&](auto done) { store->LoadMessages(7, 0, 10, done); });
    QCOMPARE(result.conversations.first().summary, QString("[本机已删除消息]"));
    result = awaitStore([&](auto done) { store->DeleteLocalMessage(7, 3, done); });
    QVERIFY(result.error.isEmpty());
    store->CloseAccount();
    result = awaitStore([&](auto done) { store->OpenAccount(201, server, done); });
    QVERIFY(result.error.isEmpty());
    result = awaitStore([&](auto done) { store->SaveConversations({{7, 102}}, done); });
    result = awaitStore(
        [&](auto done) { store->SaveMessages({record(2, "another account")}, 0, -1, done); });
    QVERIFY(!result.suppressedMessageKeys.contains("7:2"));
    result = awaitStore([&](auto done) { store->LoadMessages(7, 0, 10, done); });
    QCOMPARE(result.messages.first().content, QString("another account"));
  }
  void failedDeletionRollsBackTombstoneAndPreservesMessage()
  {
    seed({record(1, "keep")});
    sql("CREATE TRIGGER deny_message_delete BEFORE DELETE ON messages BEGIN "
        "SELECT RAISE(ABORT,'fixture deletion failure'); END");
    QSignalSpy deleted(store, &ChatStore::localMessageDeleted);
    auto result = awaitStore([&](auto done) { store->DeleteLocalMessage(7, 1, done); });
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(deleted.size(), 0);
    sql("DROP TRIGGER deny_message_delete");
    result = awaitStore(
        [&](auto done) { store->SaveMessages({record(1, "still present")}, 0, -1, done); });
    QVERIFY(result.error.isEmpty());
    QVERIFY(result.suppressedMessageKeys.isEmpty());
    result = awaitStore([&](auto done) { store->LoadMessages(7, 0, 10, done); });
    QCOMPARE(result.messages.first().content, QString("still present"));
    result = awaitStore([&](auto done) { store->DeleteLocalMessage(7, 0, done); });
    QVERIFY(!result.error.isEmpty());
  }
  void versionOneUpgradePreservesMessagesAndRejectsFutureSchema()
  {
    seed({record(1, "version-one record")});
    sql("DROP TABLE deleted_messages");
    sql("PRAGMA user_version=1");
    store->CloseAccount();
    auto result = awaitStore([&](auto done) { store->OpenAccount(101, server, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    result = awaitStore([&](auto done) { store->LoadMessages(7, 0, 10, done); });
    QCOMPARE(result.messages.first().content, QString("version-one record"));
    result = awaitStore([&](auto done) { store->DeleteLocalMessage(7, 1, done); });
    QVERIFY(result.error.isEmpty());
    store->CloseAccount();
    result = awaitStore([&](auto done) { store->OpenAccount(101, server, done); });
    QVERIFY(result.error.isEmpty());
    result = awaitStore([&](auto done) { store->LoadMessages(7, 0, 10, done); });
    QVERIFY(result.messages.isEmpty());
    sql("PRAGMA user_version=2147483647");
    store->CloseAccount();
    result = awaitStore([&](auto done) { store->OpenAccount(101, server, done); });
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!store->IsReady());
  }
  void batchDeletionRollsBackAndTombstonesSurviveReplay()
  {
    seed({record(1, "first"), record(2, "second"), record(3, "keep")});
    sql("CREATE TRIGGER reject_batch BEFORE INSERT ON deleted_messages "
        "WHEN NEW.server_id=2 BEGIN SELECT RAISE(ABORT,'fixture rollback'); END");
    auto result = awaitStore([&](auto done) { store->DeleteLocalMessages(7, {1, 2}, done); });
    QVERIFY(!result.error.isEmpty());
    result = awaitStore([&](auto done) { store->LoadMessages(7, 0, 10, done); });
    QCOMPARE(result.messages.size(), 3);
    sql("DROP TRIGGER reject_batch");
    result = awaitStore([&](auto done) { store->DeleteLocalMessages(7, {1, 0}, done); });
    QVERIFY(!result.error.isEmpty());
    QSignalSpy deleted(store, &ChatStore::localMessagesDeleted);
    result = awaitStore([&](auto done) { store->DeleteLocalMessages(7, {1, 2}, done); });
    QVERIFY(result.error.isEmpty());
    QCOMPARE(deleted.size(), 1);
    result = awaitStore([&](auto done) {
      store->SaveMessages({record(1, "replayed first"), record(2, "replayed second")}, 7, 2, done);
    });
    QVERIFY(result.error.isEmpty());
    result = awaitStore([&](auto done) { store->LoadMessages(7, 0, 10, done); });
    QCOMPARE(result.messages.size(), 1);
    QCOMPARE(result.messages.first().serverId, 3);
  }

  void actualMultiSelectionFavoritesForwardAndDeletionPreserveDraft()
  {
    seed({record(1, "first selection"), record(2, "second selection")});
    ChatSyncService sync;
    sync.StartSession(101, server);
    QTRY_VERIFY(sync.IsReady());
    QDialog shell;
    shell.setObjectName("ChatDialog");
    shell.resize(1000, 700);
    ChatPage page(&shell);
    page.setObjectName("chat_page");
    auto *layout = new QVBoxLayout(&shell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(&page);
    page.SetChatData(data);
    connect(&sync, &ChatSyncService::messagesChanged, &page, [&](int thread) {
      if (thread == 7)
        page.SetChatData(data);
    });
    shell.show();
    auto *edit = page.findChild<MessageTextEdit *>();
    QTRY_VERIFY(edit->isEnabled());
    edit->setPlainText("keep editor draft");
    auto *popup = menu(page, "first selection");
    QVERIFY(popup);
    popup->findChild<QPushButton *>("search_btn")->click();
    QVERIFY(page.findChild<ChatHistoryDialog *>()->isVisible());
    page.findChild<ChatHistoryDialog *>()->reject();
    popup = menu(page, "first selection");
    popup->findChild<QPushButton *>("select_btn")->click();
    auto *bar = page.findChild<QWidget *>("messageSelectionBar");
    QVERIFY(bar && bar->isVisible());
    QCOMPARE(page.findChild<QLabel *>("messageSelectionCount")->text(),
             QString("已选择 1 / 50 条"));
    for (auto *item : page.findChildren<ChatItemBase *>())
      if (item->property("message_content") == "second selection")
        item->findChild<QCheckBox *>("messageSelection")->setChecked(true);
    QCOMPARE(page.findChild<QLabel *>("messageSelectionCount")->text(),
             QString("已选择 2 / 50 条"));
    const auto artifacts = qEnvironmentVariable("CPPCHAT_TEST_ARTIFACTS");
    if (!artifacts.isEmpty()) {
      QVERIFY(QDir().mkpath(artifacts));
      QVERIFY(page.grab().save(artifacts + "/message-selection.png"));
    }
    QSignalSpy favorites(store, &ChatStore::favoritesChanged);
    page.findChild<QPushButton *>("selectionFavorite")->click();
    QTRY_COMPARE(favorites.size(), 1);
    auto result =
        awaitStore([&](auto done) { store->LoadFavorites({}, FavoriteCategory::All, 0, done); });
    QCOMPARE(result.favorites.size(), 2);
    QSignalSpy forwarded(&page, &ChatPage::sig_forward_messages);
    page.findChild<QPushButton *>("selectionForward")->click();
    QTRY_COMPARE(forwarded.size(), 1);
    const auto payload = qvariant_cast<QVector<StoredFavorite>>(forwarded.first().first());
    QCOMPARE(payload.size(), 2);
    QCOMPARE(payload.first().content, QString("first selection"));
    QCOMPARE(payload.last().content, QString("second selection"));
    QSignalSpy deleted(store, &ChatStore::localMessagesDeleted);
    chooseDeletion(true);
    page.findChild<QPushButton *>("selectionDelete")->click();
    QTRY_COMPARE(deleted.size(), 1);
    QTRY_VERIFY(data->GetMsgMapRef().isEmpty());
    QCOMPARE(edit->toPlainText(), QString("keep editor draft"));
    result =
        awaitStore([&](auto done) { store->LoadFavorites({}, FavoriteCategory::All, 0, done); });
    QCOMPARE(result.favorites.size(), 2);
    page.findChild<QPushButton *>("selectionCancel")->click();
    QVERIFY(!bar->isVisible());
    sync.ResetSession();
  }

  void selectionCancelsOnConversationChangeAndEscape()
  {
    seed({record(1, "selection context")});
    ChatPage page;
    page.SetChatData(data);
    page.show();
    QTRY_VERIFY(page.findChild<MessageTextEdit *>()->isEnabled());
    menu(page, "selection context")->findChild<QPushButton *>("select_btn")->click();
    auto *bar = page.findChild<QWidget *>("messageSelectionBar");
    QVERIFY(bar->isVisible());
    QTest::keyClick(page.findChild<MessageTextEdit *>(), Qt::Key_Escape);
    QVERIFY(!bar->isVisible());
    menu(page, "selection context")->findChild<QPushButton *>("select_btn")->click();
    QVERIFY(bar->isVisible());
    auto other = std::make_shared<ChatThreadData>(103, 8, 0);
    UserMgr::GetInstance()->AddChatThreadData(other, 103);
    page.SetChatData(other);
    QVERIFY(!bar->isVisible());
    page.SetChatData(data);
    QVERIFY(!bar->isVisible());
  }

  void readerAndQuoteUseActualMessageMenuAndPreserveDraft()
  {
    const QString body = "<b>原样显示</b>\nhttps://example.test/远程链接\n中文 😀";
    seed({record(1, body)});
    ChatPage page;
    page.resize(760, 680);
    page.SetChatData(data);
    page.show();
    auto *edit = page.findChild<MessageTextEdit *>();
    QTRY_VERIFY(edit->isEnabled());
    edit->setPlainText("已写好的回复");
    edit->selectAll();
    QSignalSpy network(TcpMgr::GetInstance().get(), &TcpMgr::sig_send_data);
    auto *popup = menu(page, body);
    QVERIFY(popup);
    popup->findChild<QPushButton *>("quote_btn")->click();
    QVERIFY(edit->toPlainText().startsWith("> 同事：\n> <b>原样显示</b>"));
    QVERIFY(edit->toPlainText().endsWith("已写好的回复"));
    QCOMPARE(network.size(), 0);
    popup = menu(page, body);
    QVERIFY(popup);
    popup->findChild<QPushButton *>("read_btn")->click();
    auto *reader = page.findChild<QDialog *>("messageReader");
    QVERIFY(reader && reader->isVisible());
    auto *text = reader->findChild<QPlainTextEdit *>("messageReaderText");
    QVERIFY(text && text->isReadOnly());
    QCOMPARE(text->toPlainText(), body);
    QVERIFY(text->font().pointSizeF() >= 18);
    const int initialHeight = text->fontMetrics().height();
    for (auto *button : reader->findChildren<QPushButton *>())
      if (button->text() == "放大")
        button->click();
    QVERIFY(text->fontMetrics().height() > initialHeight);
    for (auto *button : reader->findChildren<QPushButton *>())
      if (button->text() == "缩小")
        button->click();
    QCOMPARE(text->fontMetrics().height(), initialHeight);
    for (auto *button : reader->findChildren<QPushButton *>())
      if (button->text() == "复制全文")
        button->click();
    QCOMPARE(QApplication::clipboard()->text(), body);
    const auto artifacts = qEnvironmentVariable("CPPCHAT_TEST_ARTIFACTS");
    if (!artifacts.isEmpty()) {
      QVERIFY(reader->grab().save(artifacts + "/message-reader.png"));
      QVERIFY(page.grab().save(artifacts + "/message-quote.png"));
    }
    auto other = std::make_shared<ChatThreadData>(103, 8, 0);
    UserMgr::GetInstance()->AddChatThreadData(other, 103);
    page.SetChatData(other);
    QVERIFY(!reader->isVisible());
    page.SetChatData(data);
    QTRY_VERIFY(edit->isEnabled());
    QVERIFY(edit->toPlainText().contains("> 同事："));
    QVERIFY(edit->toPlainText().endsWith("已写好的回复"));
    QCOMPARE(network.size(), 0);
  }
  void quoteUndoAttachmentsPersistenceAndUnicodeBounds()
  {
    MessageTextEdit edit;
    edit.setPlainText("draft");
    QImage image(4, 4, QImage::Format_RGB32);
    image.fill(Qt::red);
    edit.moveCursor(QTextCursor::End);
    edit.insertImage(image);
    QTRY_VERIFY(edit.attachmentsReady());
    const auto original = edit.messages();
    QVERIFY(!original.last().imageData.isEmpty());
    edit.selectAll();
    edit.insertQuote("同事", "正文");
    const auto quote = edit.messages();
    QCOMPARE(quote.last().msgFlag, QString("image"));
    QCOMPARE(quote.last().imageData, original.last().imageData);
    QVERIFY(quote.first().content.startsWith("> 同事：\n> 正文\n\n"));
    store->SaveDraft(7, quote);
    auto result = awaitStore([&](auto done) { store->LoadDraft(7, done); });
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.draft.first().content, quote.first().content);
    QCOMPARE(result.draft.last().imageData, quote.last().imageData);
    edit.undo();
    QCOMPARE(edit.messages().first().content, original.first().content);
    QCOMPARE(edit.messages().last().imageData, original.last().imageData);
    edit.clear();
    edit.insertQuote("sender", QString(1023, 'x') + QString::fromUcs4(U"😀tail"));
    QVERIFY(edit.toPlainText().contains("> …"));
    const auto text = edit.toPlainText();
    for (int i = 0; i < text.size(); ++i)
      if (text.at(i).isHighSurrogate())
        QVERIFY(i + 1 < text.size() && text.at(++i).isLowSurrogate());
    edit.clear();
    edit.insertQuote("sender", QString("line\n").repeated(20));
    QVERIFY(edit.toPlainText().count("> line") == 8);
    QVERIFY(edit.toPlainText().contains("> …"));
  }
  void deletionUpdatesVisibleHistoryAndSuppressesDuplicateDelivery()
  {
    seed({record(1, "sensitive-preview")});
    ChatSyncService sync;
    sync.StartSession(101, server);
    QTRY_VERIFY(sync.IsReady());
    ChatPage page;
    page.resize(760, 680);
    page.SetChatData(data);
    connect(&sync, &ChatSyncService::messagesChanged, &page, [&](int thread) {
      if (thread == 7)
        page.SetChatData(data);
    });
    page.show();
    QTRY_VERIFY(page.findChild<MessageTextEdit *>()->isEnabled());
    auto *popup = menu(page, "sensitive-preview");
    QVERIFY(popup && popup->findChild<QPushButton *>("delete_btn")->isEnabled());
    chooseDeletion(false);
    QSignalSpy network(TcpMgr::GetInstance().get(), &TcpMgr::sig_send_data);
    popup->findChild<QPushButton *>("delete_btn")->click();
    QVERIFY(data->GetMsgMapRef().contains(1));
    popup = menu(page, "sensitive-preview");
    QVERIFY(popup);
    chooseDeletion(true);
    QSignalSpy deleted(store, &ChatStore::localMessageDeleted),
        incoming(&sync, &ChatSyncService::incomingMessage);
    popup->findChild<QPushButton *>("delete_btn")->click();
    QTRY_COMPARE(deleted.size(), 1);
    QTRY_VERIFY(!data->GetMsgMapRef().contains(1));
    QVERIFY(data->GetLastMsg() != "sensitive-preview");
    sync.ReceiveMessages({record(1, "sensitive-preview").ToMessage()}, false);
    auto result = awaitStore([&](auto done) { store->SaveConversations({}, done); });
    QTest::qWait(20);
    QVERIFY(!data->GetMsgMapRef().contains(1));
    QVERIFY(page.findChildren<ChatItemBase *>().isEmpty());
    QCOMPARE(incoming.size(), 0);
    QCOMPARE(data->GetLastMsg(), QString("[本机已删除消息]"));
    QCOMPARE(network.size(), 0);
    sync.ResetSession();
  }
  void lateAcknowledgmentAfterClearingHistoryDoesNotLeavePendingGhost()
  {
    seed({record(2, "confirmed later")});
    auto pending = record(0, "waiting for acknowledgement");
    pending.senderUid = 101;
    pending.uniqueId = "delayed-ack";
    auto result = awaitStore([&](auto done) { store->SaveMessages({pending}, 0, -1, done); });
    QVERIFY(result.error.isEmpty());
    data->AppendUnRspMsg(pending.uniqueId, pending.ToMessage());
    ChatSyncService sync;
    sync.StartSession(101, server);
    QTRY_VERIFY(sync.IsReady());
    result = awaitStore([&](auto done) { store->ClearLocalHistory(7, done); });
    QVERIFY(result.error.isEmpty());
    data->ClearLocalMessages();
    pending.serverId = 1;
    sync.ReceiveMessages({pending.ToMessage()}, true);
    result = awaitStore([&](auto done) { store->SaveConversations({}, done); });
    QTRY_VERIFY(data->GetPendingMessages().empty());
    QTRY_VERIFY(data->GetMsgMapRef().isEmpty());
    result = awaitStore([&](auto done) { store->LoadMessages(7, 0, 10, done); });
    QVERIFY(result.messages.isEmpty());
    sync.ResetSession();
  }
  void pendingAndImageMenusAndStaleContextStaySafe()
  {
    QWidget owner;
    owner.show();
    MessageActions actions(&owner);
    actions.SetConversation(data);
    auto message = record(1, "old context").ToMessage();
    MessageMenu *popup = new MessageMenu(&owner);
    actions.Bind(popup, message);
    QSignalSpy quote(&actions, &MessageActions::quoteRequested);
    popup->Popup(QPoint(20, 20), {false, false, false, false, false, false});
    QVERIFY(!popup->findChild<QPushButton *>("read_btn")->isEnabled());
    QVERIFY(!popup->findChild<QPushButton *>("quote_btn")->isEnabled());
    QVERIFY(!popup->findChild<QPushButton *>("delete_btn")->isEnabled());
    auto other = std::make_shared<ChatThreadData>(103, 8, 0);
    UserMgr::GetInstance()->AddChatThreadData(other, 103);
    actions.SetConversation(other);
    popup->quoteRequested();
    QCOMPARE(quote.size(), 0);
    popup->hide();
  }
  void pendingCannotDeleteAndImagesKeepTheirOwnViewer()
  {
    auto image = record(1, QString(32, 'a'));
    image.type = 1;
    seed({image});
    auto pending = record(0, "unconfirmed");
    pending.uniqueId = "pending-only";
    pending.status = SEND_FAILED;
    data->AppendUnRspMsg(pending.uniqueId, pending.ToMessage());
    ChatPage page;
    page.SetChatData(data);
    page.show();
    QTRY_VERIFY(page.findChild<MessageTextEdit *>()->isEnabled());
    auto *popup = menu(page, image.content);
    QVERIFY(popup);
    QVERIFY(!popup->findChild<QPushButton *>("read_btn")->isEnabled());
    QVERIFY(!popup->findChild<QPushButton *>("quote_btn")->isEnabled());
    QVERIFY(popup->findChild<QPushButton *>("delete_btn")->isEnabled());
    popup->hide();
    popup = menu(page, "unconfirmed");
    QVERIFY(popup);
    QVERIFY(!popup->findChild<QPushButton *>("delete_btn")->isEnabled());
    QVERIFY(popup->findChild<QPushButton *>("read_btn")->isEnabled());
    popup->hide();
  }
};
QTEST_MAIN(MessageActionsTests)
#include "message_actions_tests.moc"
