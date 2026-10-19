#include "app/thememanager.h"
#include "chatstorefixture.h"
#include "common/localstoresecurity.h"
#include "common/runtimepaths.h"
#include "dialogs/applyfriend.h"
#include "dialogs/chatdialog.h"
#include "dialogs/contactmanagerdialog.h"
#include "dialogs/contactssection.h"
#include "dialogs/messageactions.h"
#include "dialogs/messagemenu.h"
#include "dialogs/notesdialog.h"
#include "dialogs/quickactionmenu.h"
#include "dialogs/remindersdialog.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/chatsyncservice.h"
#include "services/reminderservice.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableView>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QtEndian>

using QtFixtures::awaitStore;
class OrganizerTests : public QObject {
  Q_OBJECT
  QTemporaryDir root;
  ChatStore *store = ChatStore::instance();
  int uid = 300;
  const QString server = "https://organizer.example.test";
  std::shared_ptr<ChatThreadData> data;
  StoredMessage source() const
  {
    StoredMessage value;
    value.threadId = 7;
    value.serverId = 3;
    value.senderUid = 102;
    value.uniqueId = "organizer-source";
    value.content = "<b>待办事项</b> 中文 100%_";
    value.time = "2026-10-19 12:00:00";
    return value;
  }
  void friends()
  {
    auto first = std::make_shared<UserInfo>(102, "Alice", "艾丽丝", "", 0);
    first->_back = "同事";
    first->_tags = {"工作", "C++"};
    UserMgr::GetInstance()->AddFriend(first);
    auto second = std::make_shared<UserInfo>(103, "Bob", "鲍勃", "", 0);
    second->_tags = {"家人"};
    UserMgr::GetInstance()->AddFriend(second);
  }
  LocalNote note(const QString &title = "标题", const QString &body = "私有正文 100%_") const
  {
    LocalNote value;
    value.title = title;
    value.text = body;
    return value;
  }
  LocalReminder reminder(qint64 due = 0) const
  {
    LocalReminder value;
    value.threadId = 7;
    value.serverId = 3;
    value.subject = "同事";
    value.excerpt = source().content;
    value.dueMs = due ? due : QDateTime::currentMSecsSinceEpoch() + 60000;
    return value;
  }
  ChatStoreResult save(const LocalNote &value)
  {
    return awaitStore([&](auto done) { store->SaveNote(value, done); });
  }
  ChatStoreResult schedule(const LocalReminder &value)
  {
    return awaitStore([&](auto done) { store->SaveReminder(value, done); });
  }
  void sql(const QString &statement)
  {
    const QString name = "organizer-fixture";
    {
      auto database = QSqlDatabase::addDatabase(LocalStoreSecurity::driverName(), name);
      database.setDatabaseName(store->AccountDirectory() + "/sql/db.sqlite");
      LocalStoreSecurity::configureDatabase(
          database, LocalStoreSecurity::accountKey(store->AccountDirectory()));
      QVERIFY(database.open());
      QSqlQuery query(database);
      QVERIFY2(query.exec(statement), qPrintable(query.lastError().text()));
    }
    QSqlDatabase::removeDatabase(name);
  }
  void answer(QMessageBox::StandardButton choice)
  {
    QTimer::singleShot(0, this, [choice] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      QVERIFY(box);
      QCOMPARE(box->defaultButton(), box->button(QMessageBox::No));
      box->button(choice)->click();
    });
  }
  void screenshot(QWidget &widget, const QString &name)
  {
    const auto directory = qEnvironmentVariable("CPPCHAT_TEST_ARTIFACTS");
    if (directory.isEmpty())
      return;
    QVERIFY(QDir().mkpath(directory));
    QVERIFY(widget.grab().save(directory + '/' + name + ".png"));
  }
private slots:
  void initTestCase()
  {
    QVERIFY(root.isValid());
    RuntimePaths::configure(root.filePath("config.ini"), root.path(), root.filePath("logs"));
    QFile config(RuntimePaths::configFile());
    QVERIFY(config.open(QIODevice::WriteOnly));
    config.write("[ChatServer]\ntls=false\n");
  }
  void init()
  {
    ++uid;
    gate_url_prefix = server;
    TcpMgr::GetInstance()->CloseConnection();
    auto mgr = UserMgr::GetInstance();
    mgr->Reset();
    mgr->SetUserInfo(std::make_shared<UserInfo>(uid, "self", ""));
    friends();
    data = std::make_shared<ChatThreadData>(102, 7, 0);
    mgr->AddChatThreadData(data, 102);
    auto result = awaitStore([&](auto done) { store->OpenAccount(uid, server, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    result = awaitStore([&](auto done) { store->SaveConversations({{7, 102}}, done); });
    QVERIFY(result.error.isEmpty());
    result = awaitStore([&](auto done) { store->SaveMessages({source()}, 0, -1, done); });
    QVERIFY(result.error.isEmpty());
    data->MoveMsg(source().ToMessage());
    QString error;
    QVERIFY2(ThemeManager::apply(*qApp, &error), qPrintable(error));
  }
  void cleanup()
  {
    TcpMgr::GetInstance()->CloseConnection();
    store->CloseAccount();
    UserMgr::GetInstance()->Reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void cleanupTestCase() { store->Shutdown(); }
  void notesCrudRevisionConflictLiteralSearchAndPaging()
  {
    auto result = save(note());
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    auto current = result.notes.first();
    QCOMPARE(current.revision, 1);
    QVERIFY(current.id > 0);
    current.text = "最新正文";
    auto updated = save(current);
    QVERIFY(updated.error.isEmpty());
    QCOMPARE(updated.notes.first().revision, 2);
    current.text = "旧窗口覆盖";
    QVERIFY(!save(current).error.isEmpty());
    result = awaitStore([&](auto done) { store->DeleteNote(current.id, 1, done); });
    QVERIFY(!result.error.isEmpty());
    result = awaitStore([&](auto done) { store->GetNote(current.id, done); });
    QCOMPARE(result.notes.first().text, QString("最新正文"));
    QVERIFY(save(note("literal", "only 100%_ here")).error.isEmpty());
    QVERIFY(save(note("other", "100AA")).error.isEmpty());
    result = awaitStore([&](auto done) { store->LoadNotes("%_", 0, done); });
    QCOMPARE(result.notes.size(), 1);
    sql("WITH RECURSIVE seq(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM seq WHERE x<51) INSERT INTO "
        "local_notes(title,body,created_ms,updated_ms) SELECT '分页'||x,'body',1,1 FROM seq");
    result = awaitStore([&](auto done) { store->LoadNotes("分页", 0, done); });
    QCOMPARE(result.notes.size(), 50);
    QVERIFY(result.more);
    const auto cursor = result.notes.last().id;
    result = awaitStore([&](auto done) { store->LoadNotes("分页", cursor, done); });
    QCOMPARE(result.notes.size(), 1);
    QVERIFY(!result.more);
    result = awaitStore([&](auto done) { store->DeleteNote(current.id, 2, done); });
    QVERIFY(result.error.isEmpty());
  }
  void noteLimitsAndFailedWritePreservePriorContent()
  {
    QVERIFY(!save(note("", "body")).error.isEmpty());
    QVERIFY(!save(note(QString(81, 'a'))).error.isEmpty());
    QVERIFY(!save(note("title", QString(32769, 'a'))).error.isEmpty());
    auto result = save(note("valid", QString(32768, 'a')));
    QVERIFY(result.error.isEmpty());
    auto value = result.notes.first();
    sql("CREATE TRIGGER deny_note BEFORE UPDATE ON local_notes BEGIN SELECT RAISE(ABORT,'fixture "
        "write failure'); END");
    value.text = "failed replacement";
    QSignalSpy changed(store, &ChatStore::notesChanged);
    QVERIFY(!save(value).error.isEmpty());
    QCOMPARE(changed.size(), 0);
    sql("DROP TRIGGER deny_note");
    result = awaitStore([&](auto done) { store->GetNote(value.id, done); });
    QCOMPARE(result.notes.first().text.size(), 32768);
    sql("WITH RECURSIVE seq(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM seq WHERE x<999) INSERT INTO "
        "local_notes(title,body,created_ms,updated_ms) SELECT 'limit'||x,'body',1,1 FROM seq");
    QVERIFY(!save(note("overflow")).error.isEmpty());
  }
  void remindersValidateSourceTimeCapacityAndReschedule()
  {
    auto invalid = reminder();
    invalid.serverId = 0;
    QVERIFY(!schedule(invalid).error.isEmpty());
    invalid = reminder();
    invalid.serverId = 999;
    QVERIFY(!schedule(invalid).error.isEmpty());
    invalid = reminder(QDateTime::currentMSecsSinceEpoch() - 1);
    QVERIFY(!schedule(invalid).error.isEmpty());
    invalid = reminder(QDateTime::currentMSecsSinceEpoch() + qint64(367) * 86400000);
    QVERIFY(!schedule(invalid).error.isEmpty());
    auto result = schedule(reminder());
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    auto value = result.reminders.first();
    value.dueMs += 100000;
    result = schedule(value);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.reminders.first().id, value.id);
    sql("WITH RECURSIVE seq(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM seq WHERE x<499) INSERT INTO "
        "local_reminders(thread_id,server_id,subject,excerpt,due_ms,created_ms) SELECT "
        "7,3,'limit','body',9999999999999,1 FROM seq");
    QVERIFY(!schedule(reminder()).error.isEmpty());
    result = awaitStore([&](auto done) { store->RemoveReminder(value.id, done); });
    QVERIFY(result.error.isEmpty());
    QVERIFY(schedule(reminder()).error.isEmpty());
  }
  void dueRemindersPersistWithoutRepeatedNotificationsAndSurviveDeletion()
  {
    auto result = schedule(reminder());
    QVERIFY(result.error.isEmpty());
    const auto value = result.reminders.first();
    result = awaitStore([&](auto done) { store->ClearLocalHistory(7, done); });
    QVERIFY(result.error.isEmpty());
    ReminderService service;
    QSignalSpy due(&service, &ReminderService::dueAvailable);
    QSignalSpy network(TcpMgr::GetInstance().get(), &TcpMgr::sig_send_data);
    QTest::qWait(20);
    service.CheckNow(value.dueMs + 1);
    QTRY_COMPARE(due.size(), 1);
    service.CheckNow(value.dueMs + 2);
    QTest::qWait(30);
    QCOMPARE(due.size(), 1);
    result = awaitStore([&](auto done) { store->LoadReminders(done); });
    QVERIFY(result.reminders.first().due);
    store->CloseAccount();
    result = awaitStore([&](auto done) { store->OpenAccount(uid, server, done); });
    QVERIFY(result.error.isEmpty());
    QTRY_COMPARE(due.size(), 2);
    QCOMPARE(network.size(), 0);
    result = awaitStore([&](auto done) { store->RemoveReminder(value.id, done); });
    QVERIFY(result.error.isEmpty());
  }
  void failedDueTransitionIsAtomicAndCanRetry()
  {
    auto first = schedule(reminder());
    auto second = schedule(reminder());
    QVERIFY(first.error.isEmpty() && second.error.isEmpty());
    const auto time = second.reminders.first().dueMs + 10;
    sql("CREATE TRIGGER deny_due BEFORE UPDATE ON local_reminders WHEN OLD.id=" +
        QString::number(second.reminders.first().id) +
        " BEGIN SELECT RAISE(ABORT,'fixture due failure'); END");
    auto result = awaitStore([&](auto done) { store->CheckDueReminders(time, done); });
    QVERIFY(!result.error.isEmpty());
    result = awaitStore([&](auto done) { store->LoadReminders(done); });
    for (const auto &value : result.reminders)
      QVERIFY(!value.due);
    sql("DROP TRIGGER deny_due");
    result = awaitStore([&](auto done) { store->CheckDueReminders(time, done); });
    QCOMPARE(result.reminders.size(), 2);
  }
  void rescheduledDueItemNotifiesOnceAtItsNewTime()
  {
    auto result = schedule(reminder());
    QVERIFY(result.error.isEmpty());
    auto value = result.reminders.first();
    ReminderService service;
    QSignalSpy due(&service, &ReminderService::dueAvailable);
    service.CheckNow(value.dueMs + 1);
    QTRY_COMPARE(due.size(), 1);
    value.dueMs += 120000;
    result = schedule(value);
    QVERIFY(result.error.isEmpty());
    QVERIFY(!result.reminders.first().due);
    service.CheckNow(value.dueMs - 1);
    const auto barrier = awaitStore([&](auto done) { store->LoadReminders(done); });
    QVERIFY(barrier.error.isEmpty());
    QCOMPARE(due.size(), 1);
    service.CheckNow(value.dueMs + 1);
    QTRY_COMPARE(due.size(), 2);
    service.CheckNow(value.dueMs + 10);
    const auto after = awaitStore([&](auto done) { store->LoadReminders(done); });
    QVERIFY(after.error.isEmpty());
    QCOMPARE(due.size(), 2);
  }
  void schemaFourUpgradePreservesHistoryAndFavoritesAndIsolatesAccounts()
  {
    StoredFavorite favorite;
    favorite.threadId = 7;
    favorite.serverId = 3;
    favorite.senderUid = 102;
    favorite.senderName = "同事";
    favorite.content = "favorite";
    auto result = awaitStore([&](auto done) { store->SaveFavorites({favorite}, done); });
    QVERIFY(result.error.isEmpty());
    sql("DROP TABLE local_notes");
    sql("DROP TABLE local_reminders");
    sql("PRAGMA user_version=4");
    store->CloseAccount();
    result = awaitStore([&](auto done) { store->OpenAccount(uid, server, done); });
    QVERIFY(result.error.isEmpty());
    result =
        awaitStore([&](auto done) { store->LoadFavorites({}, FavoriteCategory::All, 0, done); });
    QCOMPARE(result.favorites.size(), 1);
    QVERIFY(save(note("after-upgrade", "encrypted-note-marker-独立")).error.isEmpty());
    QVERIFY(schedule(reminder()).error.isEmpty());
    const auto path = store->AccountDirectory();
    store->CloseAccount();
    result = awaitStore([&](auto done) { store->OpenAccount(uid + 1000, server, done); });
    QVERIFY(result.error.isEmpty());
    result = awaitStore([&](auto done) { store->LoadNotes({}, 0, done); });
    QVERIFY(result.notes.isEmpty());
    result = awaitStore([&](auto done) { store->LoadReminders(done); });
    QVERIFY(result.reminders.isEmpty());
    store->CloseAccount();
    result = awaitStore(
        [&](auto done) { store->OpenAccount(uid, "https://another.example.test", done); });
    QVERIFY(result.error.isEmpty());
    result = awaitStore([&](auto done) { store->LoadNotes({}, 0, done); });
    QVERIFY(result.notes.isEmpty());
#ifdef Q_OS_WIN
    QFile encrypted(path + "/sql/db.sqlite");
    QVERIFY(encrypted.open(QIODevice::ReadOnly));
    const auto bytes = encrypted.readAll();
    QVERIFY(!bytes.startsWith("SQLite format 3"));
    QVERIFY(!bytes.contains(QString("encrypted-note-marker-独立").toUtf8()));
#endif
  }
  void noteEditorSavesRetainsConflictsCanSaveAsAndSend()
  {
    NotesDialog dialog;
    dialog.show();
    auto *title = dialog.findChild<QLineEdit *>("noteTitle");
    auto *text = dialog.findChild<QPlainTextEdit *>("noteBody");
    auto *saveButton = dialog.findChild<QPushButton *>("noteSave");
    title->setText("我的笔记");
    text->setPlainText("<b>纯文本</b>\n中文与 emoji 😀");
    QSignalSpy saved(store, &ChatStore::notesChanged);
    QSignalSpy send(&dialog, &NotesDialog::sendRequested);
    QSignalSpy network(TcpMgr::GetInstance().get(), &TcpMgr::sig_send_data);
    saveButton->click();
    QTRY_COMPARE(saved.size(), 1);
    QTRY_VERIFY(dialog.findChild<QPushButton *>("noteSend")->isEnabled());
    dialog.findChild<QPushButton *>("noteSend")->click();
    QCOMPARE(send.size(), 1);
    QVERIFY(send.first().first().toString().contains("<b>纯文本</b>"));
    auto result = awaitStore([&](auto done) { store->LoadNotes({}, 0, done); });
    auto outside = result.notes.first();
    outside.text = "别的窗口";
    QVERIFY(save(outside).error.isEmpty());
    text->setPlainText("保留我的修改");
    saveButton->click();
    QTRY_VERIFY(title->isEnabled());
    QCOMPARE(text->toPlainText(), QString("保留我的修改"));
    QVERIFY(dialog.findChild<QLabel *>("notesStatus")->text().contains("保存失败"));
    dialog.findChild<QPushButton *>("noteSaveAs")->click();
    QTRY_VERIFY(dialog.findChild<QPushButton *>("noteSend")->isEnabled());
    result = awaitStore([&](auto done) { store->LoadNotes({}, 0, done); });
    QCOMPARE(result.notes.size(), 2);
    screenshot(dialog, "notes");
    QCOMPARE(network.size(), 0);
    dialog.reject();
  }
  void unsavedNoteCloseDefaultsToKeepAndAccountChangeClearsIt()
  {
    NotesDialog dialog;
    dialog.show();
    auto *title = dialog.findChild<QLineEdit *>("noteTitle");
    title->setText("未保存");
    answer(QMessageBox::No);
    dialog.reject();
    QVERIFY(dialog.isVisible());
    QCOMPARE(title->text(), QString("未保存"));
    store->CloseAccount();
    QVERIFY(!dialog.isVisible());
    QVERIFY(title->text().isEmpty());
  }
  void reminderEditorUsesSelectedTimeAndManagementCanRescheduleAndCancel()
  {
    RemindersDialog dialog;
    dialog.show();
    dialog.Schedule(reminder());
    QPointer<QDialog> editor = dialog.findChild<QDialog *>("reminderScheduleDialog");
    QVERIFY(editor && editor->isVisible());
    editor->findChild<QDateTimeEdit *>("reminderTime")
        ->setDateTime(QDateTime::currentDateTime().addSecs(900));
    editor->findChild<QPushButton *>("reminderSave")->click();
    QTRY_VERIFY(!editor || !editor->isVisible());
    auto *list = dialog.findChild<QListWidget *>("remindersList");
    QTRY_COMPARE(list->count(), 1);
    list->setCurrentRow(0);
    screenshot(dialog, "reminders");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    dialog.findChild<QPushButton *>("reminderChange")->click();
    QPointer<QDialog> changed = dialog.findChild<QDialog *>("reminderScheduleDialog");
    QVERIFY(changed && changed->isVisible());
    changed->findChild<QDateTimeEdit *>("reminderTime")
        ->setDateTime(QDateTime::currentDateTime().addSecs(1800));
    changed->findChild<QPushButton *>("reminderSave")->click();
    QTRY_VERIFY(!changed || !changed->isVisible());
    QTRY_COMPARE(list->count(), 1);
    list->setCurrentRow(0);
    answer(QMessageBox::No);
    dialog.findChild<QPushButton *>("reminderRemove")->click();
    QCOMPARE(list->count(), 1);
    answer(QMessageBox::Yes);
    dialog.findChild<QPushButton *>("reminderRemove")->click();
    QTRY_COMPARE(list->count(), 0);
  }
  void messageReminderMenuUsesBoundSourceAndRejectsStaleContext()
  {
    QWidget owner;
    owner.show();
    MessageActions actions(&owner);
    actions.SetConversation(data);
    auto *menu = new MessageMenu(&owner);
    actions.Bind(menu, source().ToMessage());
    QSignalSpy requested(&actions, &MessageActions::reminderRequested);
    menu->Popup(QPoint(20, 20), {true, false, false, true, true, true, true});
    menu->findChild<QPushButton *>("remind_btn")->click();
    QCOMPARE(requested.size(), 1);
    const auto value = requested.first().first().value<LocalReminder>();
    QCOMPARE(value.threadId, 7);
    QCOMPARE(value.serverId, 3);
    QCOMPARE(value.excerpt, source().content);
    auto *stale = new MessageMenu(&owner);
    actions.Bind(stale, source().ToMessage());
    actions.SetConversation(nullptr);
    stale->reminderRequested();
    QCOMPARE(requested.size(), 1);
    auto *pending = new MessageMenu(&owner);
    pending->Popup(QPoint(20, 20), {});
    QVERIFY(!pending->findChild<QPushButton *>("remind_btn")->isEnabled());
  }
  void contactManagerUsesRealRowsAndCombinesSearchTagsAndStars()
  {
    ContactManagerDialog dialog;
    dialog.show();
    auto *view = dialog.findChild<QTableView *>("contactManagerTable");
    QCOMPARE(view->model()->rowCount(), 2);
    auto *search = dialog.findChild<QLineEdit *>("contactManagerSearch");
    search->setText("alice");
    QCOMPARE(view->model()->rowCount(), 1);
    search->clear();
    auto *tag = dialog.findChild<QComboBox *>("contactManagerTag");
    tag->setCurrentIndex(tag->findData("工作"));
    QCOMPARE(view->model()->rowCount(), 1);
    view->selectRow(0);
    QVERIFY(!dialog.findChild<QPushButton *>("contactManagerEdit")->isEnabled());
    dialog.findChild<QPushButton *>("contactManagerStar")->click();
    QVERIFY(UserMgr::GetInstance()->IsStarredFriend(102));
    dialog.findChild<QCheckBox *>("contactManagerStarred")->setChecked(true);
    QCOMPARE(view->model()->rowCount(), 1);
    UserMgr::GetInstance()->GetFriendById(102)->_back = "新备注";
    dialog.Refresh();
    search->setText("新备注");
    QCOMPARE(view->model()->rowCount(), 1);
    screenshot(dialog, "contact-manager");
    view->selectRow(0);
    QSignalSpy opened(&dialog, &ContactManagerDialog::openProfileRequested);
    dialog.findChild<QPushButton *>("contactManagerProfile")->click();
    QCOMPARE(opened.first().first().toInt(), 102);
  }
  void existingEntriesOpenRealNotesAndOverdueCenter()
  {
    auto result = schedule(reminder());
    const auto deadline = result.reminders.first().dueMs;
    ChatDialog dialog;
    dialog.StartSession();
    QTRY_VERIFY(dialog.findChild<ChatSyncService *>()->IsReady());
    dialog.show();
    auto *quick = dialog.findChild<QuickActionMenu *>();
    quick->Popup(QPoint(20, 20));
    quick->findChild<QPushButton *>("note_btn")->click();
    auto *notes = dialog.findChild<NotesDialog *>();
    QVERIFY(notes && notes->isVisible());
    notes->reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    dialog.findChild<QPushButton *>("side_favorites_btn")->click();
    dialog.findChild<QPushButton *>("write_note_btn")->click();
    auto *fromFavorites = dialog.findChild<NotesDialog *>();
    QVERIFY(fromFavorites && fromFavorites->isVisible());
    fromFavorites->reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    dialog.findChild<QPushButton *>("openReminders")->click();
    auto *fromHeader = dialog.findChild<RemindersDialog *>();
    QVERIFY(fromHeader && fromHeader->isVisible());
    fromHeader->reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    dialog.findChild<ReminderService *>()->CheckNow(deadline + 1);
    QTRY_VERIFY(dialog.findChild<RemindersDialog *>() &&
                dialog.findChild<RemindersDialog *>()->isVisible());
    auto *reminders =
        dialog.findChild<RemindersDialog *>()->findChild<QListWidget *>("remindersList");
    QTRY_COMPARE(reminders->count(), 1);
    QVERIFY(reminders->item(0)->text().contains("[到期]"));
    dialog.ResetSession();
  }
  void contactManagerEditsMetadataThroughActualLoopbackProtocol()
  {
    QTcpServer peer;
    QVERIFY(peer.listen(QHostAddress::LocalHost));
    int metadataRequests = 0;
    connect(&peer, &QTcpServer::newConnection, &peer, [&] {
      auto *socket = peer.nextPendingConnection();
      auto buffer = std::make_shared<QByteArray>();
      connect(socket, &QTcpSocket::readyRead, &peer, [&, socket, buffer] {
        *buffer += socket->readAll();
        while (buffer->size() >= 4) {
          const auto id = qFromBigEndian<quint16>(buffer->constData());
          const auto size = qFromBigEndian<quint16>(buffer->constData() + 2);
          if (buffer->size() < size + 4)
            return;
          auto object = QJsonDocument::fromJson(buffer->mid(4, size)).object();
          buffer->remove(0, size + 4);
          quint16 responseId = 0;
          if (id == ID_CHAT_LOGIN) {
            object = QJsonObject{
                {"error", 0},
                {"uid", uid},
                {"name", "self"},
                {"token", "organizer-loopback-token"},
                {"apply_list", QJsonArray()},
                {"friend_list",
                 QJsonArray{QJsonObject{{"uid", 102}, {"name", "Alice"}, {"nick", "艾丽丝"}}}}};
            responseId = ID_CHAT_LOGIN_RSP;
          } else if (id == ID_SET_CONTACT_TAGS_REQ) {
            ++metadataRequests;
            object["error"] = 0;
            responseId = ID_SET_CONTACT_TAGS_RSP;
          } else
            continue;
          const auto json = QJsonDocument(object).toJson(QJsonDocument::Compact);
          QByteArray frame(4, '\0');
          qToBigEndian(responseId, frame.data());
          qToBigEndian(static_cast<quint16>(json.size()), frame.data() + 2);
          socket->write(frame + json);
        }
      });
    });
    auto tcp = TcpMgr::GetInstance();
    QSignalSpy connected(tcp.get(), &TcpMgr::sig_con_success);
    tcp->slot_tcp_connect({"127.0.0.1", QString::number(peer.serverPort()), {}, uid});
    QTRY_COMPARE(connected.size(), 1);
    tcp->slot_send_data(
        ID_CHAT_LOGIN,
        QJsonDocument(QJsonObject{{"uid", uid}, {"token", "organizer-loopback-token"}}).toJson());
    QTRY_VERIFY(tcp->IsConnected());
    ContactManagerDialog dialog;
    dialog.show();
    auto *view = dialog.findChild<QTableView *>("contactManagerTable");
    view->selectRow(0);
    auto *edit = dialog.findChild<QPushButton *>("contactManagerEdit");
    QVERIFY(edit->isEnabled());
    edit->click();
    auto *form = dialog.findChild<ApplyFriend *>();
    QVERIFY(form && form->isVisible());
    form->findChild<QLineEdit *>("back_ed")->setText("经理");
    QSignalSpy metadata(tcp.get(), &TcpMgr::sig_contact_tags_updated);
    form->findChild<QPushButton *>("sure_btn")->click();
    QTRY_COMPARE(metadata.size(), 1);
    QCOMPARE(metadataRequests, 1);
    QCOMPARE(UserMgr::GetInstance()->GetFriendById(102)->_back, QString("经理"));
    dialog.findChild<QLineEdit *>("contactManagerSearch")->setText("经理");
    QCOMPARE(view->model()->rowCount(), 1);
    UserMgr::GetInstance()->RemoveFriend(102);
    tcp->sig_friend_deleted(102);
    QCOMPARE(view->model()->rowCount(), 0);
    QVERIFY(!edit->isEnabled());
  }
  void organizerTextRemainsReadableWithDarkSystemPalette()
  {
    const auto original = qApp->palette();
    const auto restore = qScopeGuard([&] { qApp->setPalette(original); });
    auto dark = original;
    dark.setColor(QPalette::Window, Qt::black);
    dark.setColor(QPalette::Base, Qt::black);
    dark.setColor(QPalette::WindowText, Qt::white);
    dark.setColor(QPalette::Text, Qt::white);
    qApp->setPalette(dark);
    NotesDialog notes;
    ContactManagerDialog contacts;
    RemindersDialog reminders;
    notes.show();
    contacts.show();
    reminders.show();
    reminders.Schedule(reminder());
    QCoreApplication::processEvents();
    const QColor body("#303236");
    QCOMPARE(notes.findChild<QLineEdit *>("noteTitle")->palette().color(QPalette::Text), body);
    QCOMPARE(notes.findChild<QPlainTextEdit *>("noteBody")->palette().color(QPalette::Text), body);
    auto *table = contacts.findChild<QTableView *>("contactManagerTable");
    QCOMPARE(table->palette().color(QPalette::Text), body);
    QCOMPARE(table->palette().color(QPalette::Base), QColor(Qt::white));
    QCOMPARE(reminders.findChild<QDateTimeEdit *>("reminderTime")->palette().color(QPalette::Text),
             body);
    screenshot(contacts, "contact-manager-system-dark");
  }
};
QTEST_MAIN(OrganizerTests)
#include "organizer_tests.moc"
