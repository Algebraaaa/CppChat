#include "app/thememanager.h"
#include "chatstorefixture.h"
#include "common/clientpreferences.h"
#include "common/runtimepaths.h"
#include "dialogs/chatdialog.h"
#include "dialogs/chathistorydialog.h"
#include "dialogs/clientsettingsdialog.h"
#include "dialogs/sidemenupopup.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/historyexporter.h"
#include "widgets/MessageTextEdit.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using QtFixtures::awaitStore;
class SettingsExportTests : public QObject {
  Q_OBJECT
  QTemporaryDir root;
  ChatStore *store = ChatStore::instance();
  int uid = 600;
  QString folder;
  const QString server = "https://export.example.test";
  HistoryFilter filter() const { return {0, {}, -1, {}, {7, 8}, true}; }
  QByteArray read(const QString &path)
  {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
      return {};
    return file.readAll();
  }
  void write(const QString &path, const QByteArray &bytes)
  {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
  }
  StoredMessage message(int thread, int id, const QString &content = "中文 100%_\n正文",
                        int type = 0)
  {
    StoredMessage value;
    value.threadId = thread;
    value.serverId = id;
    value.senderUid = 102;
    value.uniqueId = QString("export-%1-%2").arg(thread).arg(id);
    value.content = content;
    value.type = type;
    value.time = "2026-10-19 10:00:00";
    return value;
  }
  void save(const QVector<StoredMessage> &messages)
  {
    const auto result = awaitStore([&](auto done) { store->SaveMessages(messages, 0, -1, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
  }
  void chooseFile(ChatHistoryDialog &dialog, const QString &path)
  {
    QTimer::singleShot(0, &dialog, [&dialog, path] {
      auto *picker = dialog.findChild<QFileDialog *>();
      QVERIFY(picker);
      picker->selectFile(path);
      QVERIFY(QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection));
    });
    dialog.findChild<QPushButton *>("exportHistoryButton")->click();
  }
  void screenshot(QWidget &widget, const QString &name)
  {
    const auto directory = qEnvironmentVariable("CPPCHAT_TEST_ARTIFACTS");
    if (directory.isEmpty())
      return;
    QTest::qWait(80);
    QVERIFY(QDir().mkpath(directory));
    QVERIFY(widget.grab().save(directory + '/' + name + ".png"));
  }
private slots:
  void initTestCase()
  {
    QVERIFY(root.isValid());
    RuntimePaths::configure(root.filePath("config.ini"), root.path(), root.filePath("logs"));
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QString error;
    QVERIFY2(ThemeManager::apply(*qApp, &error), qPrintable(error));
  }
  void init()
  {
    folder = root.filePath(QString::number(++uid));
    QVERIFY(QDir().mkpath(folder));
    gate_url_prefix = server;
    TcpMgr::GetInstance()->CloseConnection();
    auto mgr = UserMgr::GetInstance();
    mgr->Reset();
    mgr->SetUserInfo(std::make_shared<UserInfo>(uid, "self", ""));
    auto user = std::make_shared<UserInfo>(102, "Alice", "");
    user->_back = "同事";
    mgr->AddFriend(user);
    mgr->AddFriend(std::make_shared<UserInfo>(103, "Bob", ""));
    mgr->AddChatThreadData(std::make_shared<ChatThreadData>(102, 7, 0), 102);
    mgr->AddChatThreadData(std::make_shared<ChatThreadData>(103, 8, 0), 103);
    auto result = awaitStore([&](auto done) { store->OpenAccount(uid, server, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    result = awaitStore(
        [&](auto done) { store->SaveConversations({{7, 102}, {8, 103}, {99, 199}}, done); });
    QVERIFY(result.error.isEmpty());
    QString error;
    QVERIFY(ClientPreferences::instance()->Save({}, error));
  }
  void cleanup()
  {
    store->CloseAccount();
    UserMgr::GetInstance()->Reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void cleanupTestCase() { store->Shutdown(); }
  void preferencesPersistAndDoNotChangeOnWriteFailure()
  {
    const auto path = folder + "/client.json";
    ClientPreferences prefs(path);
    QCOMPARE(prefs.Current().sendKey, ClientPreferences::SendKey::Enter);
    QString error;
    QVERIFY(prefs.Save({ClientPreferences::SendKey::ControlEnter, false}, error));
    ClientPreferences reloaded(path);
    QCOMPARE(reloaded.Current().sendKey, ClientPreferences::SendKey::ControlEnter);
    QVERIFY(!reloaded.Current().checkUpdatesOnStartup);
    QVERIFY(QFile::remove(path));
    QVERIFY(QDir().mkpath(path));
    QSignalSpy changed(&prefs, &ClientPreferences::changed);
    QVERIFY(!prefs.Save({}, error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(changed.size(), 0);
    QCOMPARE(prefs.Current().sendKey, ClientPreferences::SendKey::ControlEnter);
  }
  void malformedPreferencesUseDefaultsAndFutureVersionsArePreserved()
  {
    const auto path = folder + "/client.json";
    write(path, "{broken");
    ClientPreferences malformed(path);
    QVERIFY(!malformed.LoadError().isEmpty());
    QString error;
    QVERIFY(malformed.Save({}, error));
    const QByteArray future("{\"schema\":2,\"future\":true}");
    write(path, future);
    ClientPreferences newer(path);
    QVERIFY(!newer.Save({}, error));
    QCOMPARE(read(path), future);
  }
  void shortcutChangesAreLiveAndCompositionNeverSends()
  {
    MessageTextEdit first, second;
    first.show();
    second.show();
    first.setPlainText("draft");
    second.setPlainText("other");
    QSignalSpy sent(&first, &MessageTextEdit::send);
    QTest::keyClick(&first, Qt::Key_Return);
    QCOMPARE(sent.size(), 1);
    QTest::keyClick(&first, Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(sent.size(), 1);
    QString error;
    QVERIFY(ClientPreferences::instance()->Save({ClientPreferences::SendKey::ControlEnter, false},
                                                error));
    QVERIFY(first.placeholderText().contains("Ctrl+Enter"));
    QVERIFY(second.placeholderText().contains("Ctrl+Enter"));
    QTest::keyClick(&first, Qt::Key_Return);
    QCOMPARE(sent.size(), 1);
    QTest::keyClick(&first, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(sent.size(), 2);
    QTest::keyClick(&first, Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(sent.size(), 2);
    QKeyEvent repeated(QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier, {}, true);
    QApplication::sendEvent(&first, &repeated);
    QCOMPARE(sent.size(), 2);
    QInputMethodEvent preedit("拼", {});
    QApplication::sendEvent(&first, &preedit);
    QTest::keyClick(&first, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(sent.size(), 2);
    QInputMethodEvent commit;
    commit.setCommitString("拼音");
    QApplication::sendEvent(&first, &commit);
    QTest::keyClick(&first, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(sent.size(), 3);
    QVERIFY(first.toPlainText().contains("draft"));
    QVERIFY(first.toPlainText().contains("拼音"));
    QCOMPARE(second.toPlainText(), QString("other"));
  }
  void settingsEntrySavesAndManualUpdateRemainsAvailable()
  {
    ChatDialog host;
    host.show();
    auto *menu = host.findChild<SideMenuPopup *>();
    menu->findChild<QPushButton *>("settings_btn")->click();
    auto *dialog = host.findChild<ClientSettingsDialog *>();
    QVERIFY(dialog && dialog->isVisible());
    dialog->findChild<QComboBox *>("sendKeySetting")->setCurrentIndex(1);
    dialog->findChild<QCheckBox *>("startupUpdatesSetting")->setChecked(false);
    dialog->findChild<QPushButton *>("saveSettings")->click();
    QVERIFY(!ClientPreferences::instance()->Current().checkUpdatesOnStartup);
    QSignalSpy check(&host, &ChatDialog::checkUpdatesRequested);
    dialog->findChild<QPushButton *>("settingsCheckUpdates")->click();
    QCOMPARE(check.size(), 1);
    menu->findChild<QPushButton *>("settings_btn")->click();
    QCOMPARE(host.findChildren<ClientSettingsDialog *>().size(), 1);
    screenshot(*dialog, "client-settings");
    dialog->findChild<QPushButton *>("defaultSettings")->click();
    QVERIFY(
        !ClientPreferences::instance()->Current().checkUpdatesOnStartup); // Defaults require Save.
    host.ResetSession();
  }
  void jsonExportUsesAllPagesAndFreezesNewRows()
  {
    QVector<StoredMessage> records;
    for (int i = 1; i <= 61; ++i) {
      records.append(message(7, i));
      records.append(message(8, i));
    }
    records.append(message(99, 900, "inaccessible"));
    records.append(message(7, 0, "pending"));
    save(records);
    HistoryExporter exporter;
    QSignalSpy complete(&exporter, &HistoryExporter::finished),
        failed(&exporter, &HistoryExporter::failed);
    bool inserted = false;
    connect(&exporter, &HistoryExporter::progress, this, [&](int count) {
      if (count == 50 && !inserted) {
        inserted = true;
        save({message(7, 1000, "newly-inserted")});
      }
    });
    QString error;
    const auto path = folder + "/history.json";
    QVERIFY2(exporter.Start(filter(), path, HistoryExporter::Format::Json, error),
             qPrintable(error));
    QTRY_COMPARE(complete.size(), 1);
    QCOMPARE(failed.size(), 0);
    auto rows = QJsonDocument::fromJson(read(path)).object()["messages"].toArray();
    QCOMPARE(rows.size(), 122);
    QSet<QString> ids;
    for (auto row : rows) {
      auto m = row.toObject();
      QVERIFY(m["server_id"].toInt() > 0);
      QVERIFY(m["thread_id"].toInt() != 99);
      QVERIFY(m["content"].toString() != "newly-inserted");
      ids.insert(QString::number(m["thread_id"].toInt()) + '/' +
                 QString::number(m["server_id"].toInt()));
    }
    QCOMPARE(ids.size(), 122);
    QVERIFY(read(path).contains(QString("中文").toUtf8()));
    QVERIFY(!read(path).contains("password"));
    QVERIFY(!read(path).contains("export.example.test"));
  }
  void filterTextAndMediaMetadataExportWithoutAttachmentRequests()
  {
    save({message(7, 1, "https://example.test/100%_"), message(7, 2, "other"),
          message(8, 1, "opaque-media-id", 1)});
    HistoryExporter exporter;
    QSignalSpy complete(&exporter, &HistoryExporter::finished);
    QString error;
    auto query = filter();
    query.category = 3;
    query.text = "%_";
    query.date = "2026-10-19";
    QVERIFY(exporter.Start(query, folder + "/links.txt", HistoryExporter::Format::Text, error));
    QTRY_COMPARE(complete.size(), 1);
    QVERIFY(read(folder + "/links.txt").contains("https://example.test/100%_"));
    QVERIFY(!read(folder + "/links.txt").contains("other"));
    query = filter();
    query.category = 1;
    QVERIFY(exporter.Start(query, folder + "/images.json", HistoryExporter::Format::Json, error));
    QTRY_COMPARE(complete.size(), 2);
    const auto rows =
        QJsonDocument::fromJson(read(folder + "/images.json")).object()["messages"].toArray();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().toObject()["content"].toString(), QString("opaque-media-id"));
  }
  void cancellationAndLimitsPreserveOriginalFile()
  {
    save({message(7, 1), message(7, 2), message(7, 3)});
    const auto path = folder + "/preserved.json";
    write(path, "original");
    QString error;
    HistoryExporter exporter(nullptr, 2);
    QSignalSpy failed(&exporter, &HistoryExporter::failed);
    QVERIFY(exporter.Start(filter(), path, HistoryExporter::Format::Json, error));
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(read(path), QByteArray("original"));
    HistoryExporter bytes(nullptr, 10000, 256);
    QSignalSpy tooLarge(&bytes, &HistoryExporter::failed);
    QVERIFY(bytes.Start(filter(), path, HistoryExporter::Format::Json, error));
    QTRY_COMPARE(tooLarge.size(), 1);
    QCOMPARE(read(path), QByteArray("original"));
    HistoryExporter cancel;
    QSignalSpy done(&cancel, &HistoryExporter::finished);
    QVERIFY(cancel.Start(filter(), path, HistoryExporter::Format::Json, error));
    cancel.Cancel();
    QVERIFY(!cancel.Busy());
    QCOMPARE(read(path), QByteArray("original"));
    QVERIFY(cancel.Start(filter(), folder + "/retry.json", HistoryExporter::Format::Json, error));
    QTRY_COMPARE(done.size(), 1);
    QCOMPARE(
        QJsonDocument::fromJson(read(folder + "/retry.json")).object()["messages"].toArray().size(),
        3);
    QCOMPARE(read(path), QByteArray("original"));
  }
  void storageAccountAndPermissionChangesAbortExport()
  {
    save({message(7, 1)});
    const auto path = folder + "/original.json";
    write(path, "original");
    QString error;
    HistoryExporter exporter;
    QSignalSpy failed(&exporter, &HistoryExporter::failed);
    QVERIFY(exporter.Start(filter(), path, HistoryExporter::Format::Json, error));
    emit store->localHistoryCleared(7);
    QCOMPARE(failed.size(), 1);
    QCOMPARE(read(path), QByteArray("original"));
    QVERIFY(exporter.Start(filter(), path, HistoryExporter::Format::Json, error));
    emit TcpMgr::GetInstance() -> sig_friend_deleted(102);
    QCOMPARE(failed.size(), 2);
    QCOMPARE(read(path), QByteArray("original"));
    QVERIFY(exporter.Start(filter(), path, HistoryExporter::Format::Json, error));
    store->CloseAccount();
    QVERIFY(!exporter.Busy());
    QCOMPARE(read(path), QByteArray("original"));
  }
  void invalidDestinationsAndEmptyScopeNeverExposeOtherRecords()
  {
    save({message(7, 1), message(99, 1, "private")});
    HistoryExporter exporter;
    QString error;
    QVERIFY(!exporter.Start(filter(), folder + "/missing/out.json", HistoryExporter::Format::Json,
                            error));
    QVERIFY(!exporter.Start(filter(), folder + "/wrong.txt", HistoryExporter::Format::Json, error));
    QVERIFY(!exporter.Start(filter(), store->AccountDirectory() + "/data.json",
                            HistoryExporter::Format::Json, error));
    QVERIFY(!exporter.Start(filter(), RuntimePaths::preferencesDirectory() + "/client.json",
                            HistoryExporter::Format::Json, error));
    auto query = filter();
    query.allowedThreads = {};
    QSignalSpy done(&exporter, &HistoryExporter::finished);
    QVERIFY(exporter.Start(query, folder + "/empty.json", HistoryExporter::Format::Json, error));
    QTRY_COMPARE(done.size(), 1);
    QCOMPARE(
        QJsonDocument::fromJson(read(folder + "/empty.json")).object()["messages"].toArray().size(),
        0);
  }
  void midExportCancellationAndRestartIgnoreOldCallbacks()
  {
    QVector<StoredMessage> records;
    for (int i = 1; i <= 110; ++i)
      records.append(message(7, i));
    save(records);
    HistoryExporter exporter;
    QSignalSpy done(&exporter, &HistoryExporter::finished);
    const auto original = folder + "/original.json", retry = folder + "/retry.json";
    write(original, "old contents");
    bool restarted = false;
    connect(&exporter, &HistoryExporter::progress, this, [&](int count) {
      if (!restarted && count == 50) {
        restarted = true;
        exporter.Cancel();
        QString error;
        QVERIFY(exporter.Start(filter(), retry, HistoryExporter::Format::Json, error));
      }
    });
    QString error;
    QVERIFY(exporter.Start(filter(), original, HistoryExporter::Format::Json, error));
    QTRY_COMPARE(done.size(), 1);
    QCOMPARE(read(original), QByteArray("old contents"));
    QCOMPARE(QJsonDocument::fromJson(read(retry)).object()["messages"].toArray().size(), 110);
  }
  void queryAndCommitErrorsDoNotProducePartialOutput()
  {
    save({message(7, 1)});
    HistoryExporter exporter;
    QSignalSpy failed(&exporter, &HistoryExporter::failed);
    QString error;
    const auto invalid = folder + "/invalid.json", blocked = folder + "/blocked.json";
    write(invalid, "old contents");
    auto query = filter();
    query.date = "invalid";
    QVERIFY(exporter.Start(query, invalid, HistoryExporter::Format::Json, error));
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(read(invalid), QByteArray("old contents"));
    connect(&exporter, &HistoryExporter::progress, this,
            [&](int) { QVERIFY(QDir().mkpath(blocked)); });
    QVERIFY(exporter.Start(filter(), blocked, HistoryExporter::Format::Json, error));
    QTRY_COMPARE(failed.size(), 2);
    QVERIFY(QFileInfo(blocked).isDir());
    QCOMPARE(QDir(folder).entryList(QDir::Files | QDir::Hidden), QStringList{"invalid.json"});
  }
  void actualExportButtonUsesSelectedFilterAndSafeFilePickerLifecycle()
  {
    save({message(7, 1, "chosen"), message(8, 1, "other")});
    ChatHistoryDialog dialog;
    dialog.SetAllConversations();
    dialog.show();
    auto *search = dialog.findChild<QLineEdit *>("search_edit");
    search->setText("chosen");
    auto *list = dialog.findChild<QListWidget *>("results_list");
    QTRY_COMPARE(list->count(), 1);
    chooseFile(dialog, folder + "/ui.json");
    QTRY_VERIFY(dialog.findChild<QLabel *>("exportHistoryStatus")->text().contains("已导出 1"));
    QCOMPARE(
        QJsonDocument::fromJson(read(folder + "/ui.json")).object()["messages"].toArray().size(),
        1);
    screenshot(dialog, "export-complete");
    QPointer<ChatHistoryDialog> changing = new ChatHistoryDialog;
    changing->setAttribute(Qt::WA_DeleteOnClose);
    changing->SetAllConversations();
    changing->show();
    QTimer::singleShot(0, this, [this] { store->CloseAccount(); });
    changing->findChild<QPushButton *>("exportHistoryButton")->click();
    QTRY_VERIFY(!changing);
  }
};
QTEST_MAIN(SettingsExportTests)
#include "settings_export_tests.moc"
