#include "common/chatstore.h"
#include "common/conversationpreferences.h"
#include "common/emojicatalog.h"
#include "common/inputvalidator.h"
#include "common/runtimepaths.h"
#include "dialogs/discoverpage.h"
#include "dialogs/favoritespage.h"
#include "dialogs/logindialog.h"
#include "dialogs/resetdialog.h"
#include "widgets/capturegeometry.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStackedWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <stdexcept>

namespace {
ChatStoreResult awaitStore(const std::function<void(ChatStore::Callback)> &operation)
{
  struct State {
    bool complete = false;
    bool guiThread = false;
    ChatStoreResult result;
  };
  auto state = std::make_shared<State>();
  QEventLoop loop;
  QPointer<QEventLoop> guard(&loop);
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  operation([state, guard](const ChatStoreResult &result) {
    state->result = result;
    state->complete = true;
    state->guiThread = QThread::currentThread() == QCoreApplication::instance()->thread();
    if (guard)
      guard->quit();
  });
  if (!state->complete) {
    deadline.start(8000);
    loop.exec();
  }
  if (!state->complete)
    throw std::runtime_error("ChatStore callback timed out");
  if (!state->guiThread)
    throw std::runtime_error("ChatStore callback left the GUI thread");
  return state->result;
}
StoredMessage message(int id, const QString &text)
{
  StoredMessage value;
  value.threadId = 7;
  value.senderUid = 101;
  value.serverId = id;
  value.uniqueId = "msg-" + QString::number(id);
  value.content = text;
  value.time = "2026-10-12 10:00:00";
  value.status = 2;
  return value;
}
} // namespace
class ClientTests : public QObject {
  Q_OBJECT
  QTemporaryDir root;
  ChatStore *store = nullptr;
  std::unique_ptr<QWidget> inspectedWidget;
  std::unique_ptr<QWidget> secondaryWidget;
private slots:
  void initTestCase()
  {
    QVERIFY(root.isValid());
    RuntimePaths::configure(root.path() + "/test.ini", root.path(), root.path() + "/logs");
    gate_url_prefix = "http://127.0.0.1:1";
    store = ChatStore::instance();
    QVERIFY(QSqlDatabase::isDriverAvailable("QSQLITE"));
  }
  void init()
  {
    const auto result = awaitStore(
        [&](auto done) { store->OpenAccount(101, QUuid::createUuid().toString(), done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QVERIFY(store->AccountDirectory().startsWith(root.path()));
    const auto saved =
        awaitStore([&](auto done) { store->SaveConversations({{7, 102, 0, "", ""}}, done); });
    QVERIFY2(saved.error.isEmpty(), qPrintable(saved.error));
  }
  void cleanup()
  {
    if (QTest::currentTestFailed()) {
      const QString artifacts = qEnvironmentVariable("CPPCHAT_TEST_ARTIFACTS");
      if (!artifacts.isEmpty()) {
        QDir().mkpath(artifacts);
        for (auto *widget : QApplication::topLevelWidgets())
          if (widget->isVisible())
            widget->grab().save(artifacts + '/' + QTest::currentTestFunction() + ".png");
      }
    }
    store->CloseAccount();
    inspectedWidget.reset();
    secondaryWidget.reset();
  }
  void cleanupTestCase() { store->Shutdown(); }
  void storagePersistenceAndCursor()
  {
    auto response = awaitStore([&](auto done) {
      store->SaveMessages({message(1, "中文 😃"), message(2, "hello")}, 7, 2, done);
    });
    QVERIFY2(response.error.isEmpty(), qPrintable(response.error));
    QCOMPARE(response.newMessageKeys.size(), 2);
    response =
        awaitStore([&](auto done) { store->SaveMessages({message(2, "hello")}, 0, -1, done); });
    QVERIFY(response.error.isEmpty());
    QCOMPARE(response.newMessageKeys.size(), 0);
    response = awaitStore([&](auto done) { store->LoadMessages(7, 0, 1, done); });
    QCOMPARE(response.messages.size(), 1);
    QVERIFY(response.more);
    QCOMPARE(response.messages[0].serverId, 2);
    response = awaitStore([&](auto done) { store->LoadMessages(7, 2, 10, done); });
    QCOMPARE(response.messages.size(), 1);
    QCOMPARE(response.messages[0].content, QString::fromUtf8("中文 😃"));
  }
  void walAndLegacySnapshot()
  {
    const QString server = "legacy-" + QUuid::createUuid().toString();
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(server.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
    const QString legacy = root.path() + "/accounts/" + key + "/101";
    QVERIFY(QDir().mkpath(legacy));
    const QString name = "test-legacy";
    auto cleanupDatabase = qScopeGuard([&] { QSqlDatabase::removeDatabase(name); });
    auto database = QSqlDatabase::addDatabase("QSQLITE", name);
    database.setDatabaseName(legacy + "/chat.db");
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec("PRAGMA journal_mode=WAL"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QString("wal"));
    query.finish();
    QFile schema(":/storage/chatstore-schema.sql");
    QVERIFY(schema.open(QIODevice::ReadOnly));
    for (const auto &statement : QString::fromUtf8(schema.readAll()).split(';'))
      if (!statement.trimmed().isEmpty())
        QVERIFY(query.exec(statement));
    QVERIFY(query.exec("INSERT INTO conversations(thread_id,other_uid) VALUES(7,102)"));
    QVERIFY(query.exec("INSERT INTO "
                       "messages(thread_id,sender_uid,server_id,unique_id,content,message_type,"
                       "chat_time) VALUES(7,101,1,'legacy','WAL message',0,'2026-10-12')"));
    // Keep the source connection open so VACUUM must include committed WAL data.
    auto result = awaitStore([&](auto done) { store->OpenAccount(101, server, done); });
    query.finish();
    database.close();
    if (!result.error.isEmpty())
      result = awaitStore([&](auto done) { store->OpenAccount(101, server, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QVERIFY(QFile::exists(legacy + "/sql/db.sqlite"));
    result = awaitStore([&](auto done) { store->LoadMessages(7, 0, 50, done); });
    QCOMPARE(result.messages.size(), 1);
    QCOMPARE(result.messages[0].content, QString("WAL message"));
    query.finish();
    database.close();
  }
  void inputBoundary_data()
  {
    QTest::addColumn<QString>("kind");
    QTest::addColumn<QString>("input");
    QTest::addColumn<bool>("valid");
    const auto row = [](const char *name, const QString &kind, const QString &input, bool valid) {
      QTest::newRow(name) << kind << input << valid;
    };
    row("user2", "user", "ab", false);
    row("user3", "user", "abc", true);
    row("user20", "user", QString(20, 'a'), true);
    row("user21", "user", QString(21, 'a'), false);
    row("userChinese", "user", "中文用户", true);
    row("userControl", "user", "a\nb", false);
    row("mailValid", "email", "test@example.test", true);
    row("mailInvalid", "email", "invalid", false);
    row("password7", "password", "Ab12345", false);
    row("password8", "password", "Ab123456", true);
    row("password32", "password", "A1" + QString(30, 'a'), true);
    row("password33", "password", "A1" + QString(31, 'a'), false);
    row("passwordSpace", "password", "Ab123 456", false);
    row("passwordNoDigit", "password", "abcdefgh", false);
    row("code5", "code", "12345", false);
    row("code6", "code", "123456", true);
    row("code7", "code", "1234567", false);
  }
  void inputBoundary()
  {
    QFETCH(QString, kind);
    QFETCH(QString, input);
    QFETCH(bool, valid);
    const QString error = kind == "user"       ? InputValidator::userError(input)
                          : kind == "email"    ? InputValidator::emailError(input)
                          : kind == "password" ? InputValidator::passwordError(input)
                                               : InputValidator::verifyCodeError(input);
    QCOMPARE(error.isEmpty(), valid);
  }
  void invalidPageRollsBack()
  {
    QSignalSpy errors(store, &ChatStore::storageError);
    const auto failed = awaitStore([&](auto done) {
      store->SaveMessages({message(2, "second"), message(1, "first")}, 7, 1, done);
    });
    QVERIFY(!failed.error.isEmpty());
    QVERIFY(!errors.isEmpty());
    const auto loaded = awaitStore([&](auto done) { store->LoadMessages(7, 0, 50, done); });
    QCOMPARE(loaded.messages.size(), 0);
  }
  void partialMessageWriteRollsBack()
  {
    auto invalid = message(2, "invalid");
    invalid.senderUid = 0;
    const auto failed = awaitStore([&](auto done) {
      store->SaveMessages({message(1, "must roll back"), invalid}, 0, -1, done);
    });
    QVERIFY(!failed.error.isEmpty());
    const auto loaded = awaitStore([&](auto done) { store->LoadMessages(7, 0, 50, done); });
    QVERIFY(loaded.error.isEmpty());
    QCOMPARE(loaded.messages.size(), 0);
  }
  void searchLiteralAndClearBoundary()
  {
    auto result = awaitStore([&](auto done) {
      store->SaveMessages({message(1, "literal %_ 中文"), message(2, "other")}, 0, -1, done);
    });
    QVERIFY(result.error.isEmpty());
    result = awaitStore([&](auto done) { store->SearchMessages(7, "%_", -1, {}, done); });
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.messages.size(), 1);
    result = awaitStore([&](auto done) { store->ClearLocalHistory(7, done); });
    QVERIFY(result.error.isEmpty());
    result = awaitStore([&](auto done) {
      store->SaveMessages({message(2, "other"), message(3, "new")}, 0, -1, done);
    });
    QVERIFY(result.error.isEmpty());
    result = awaitStore([&](auto done) { store->LoadMessages(7, 0, 50, done); });
    QCOMPARE(result.messages.size(), 1);
    QCOMPARE(result.messages[0].serverId, 3);
  }
  void draftAndPendingRecover()
  {
    const QString server = "restart-server";
    auto result = awaitStore([&](auto done) { store->OpenAccount(101, server, done); });
    QVERIFY(result.error.isEmpty());
    result = awaitStore([&](auto done) { store->SaveConversations({{7, 102, 0, "", ""}}, done); });
    QVERIFY(result.error.isEmpty());
    store->SaveDraft(7, {{"text", "unsent 中文", {}, {}}});
    auto pending = message(0, "pending");
    pending.uniqueId = "pending-id";
    result = awaitStore([&](auto done) { store->SaveMessages({pending}, 0, -1, done); });
    QVERIFY(result.error.isEmpty());
    store->CloseAccount();
    result = awaitStore([&](auto done) { store->OpenAccount(101, server, done); });
    QVERIFY(result.error.isEmpty());
    result = awaitStore([&](auto done) { store->LoadDraft(7, done); });
    QCOMPARE(result.draft.size(), 1);
    QCOMPARE(result.draft[0].content, QString::fromUtf8("unsent 中文"));
  }
  void attachmentIntegrityAndIsolation()
  {
    const QByteArray bytes("test-image-bytes");
    auto result = awaitStore([&](auto done) { store->SaveAttachment("resource-1", bytes, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    result = awaitStore([&](auto done) { store->LoadAttachment("resource-1", done); });
    QCOMPARE(result.image, bytes);
    const QString path =
        store->AccountDirectory() + "/attachments/" +
        QString::fromLatin1(
            QCryptographicHash::hash(QByteArray("resource-1"), QCryptographicHash::Sha256).toHex());
    QFile corrupt(path);
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("corrupt");
    corrupt.close();
    result = awaitStore([&](auto done) { store->LoadAttachment("resource-1", done); });
    QVERIFY(result.image.isEmpty());
    const QString first = store->AccountDirectory();
    result = awaitStore([&](auto done) { store->OpenAccount(102, "other-server", done); });
    QVERIFY(result.error.isEmpty());
    QVERIFY(store->AccountDirectory() != first);
    result = awaitStore([&](auto done) { store->LoadDraft(7, done); });
    QVERIFY(result.draft.isEmpty());
  }
  void filesystemFailureIsReported()
  {
    QTemporaryDir isolated;
    const QString filePath = isolated.path() + "/file";
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    RuntimePaths::configure({}, filePath, {});
    const auto result = awaitStore([&](auto done) { store->OpenAccount(101, "blocked", done); });
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!store->IsReady());
    RuntimePaths::configure(root.path() + "/test.ini", root.path(), root.path() + "/logs");
  }
  void installedDirectoryStorage()
  {
    if (qEnvironmentVariable("CPPCHAT_INSTALLED_PATH_PROBE") != "1") {
      const QString base = qEnvironmentVariable("CPPCHAT_INSTALL_TEST_ROOT", root.path());
      QTemporaryDir installation(QDir(base).filePath("CppChat-installed-XXXXXX"));
      QVERIFY(installation.isValid());
      const QString executable =
          QDir(installation.path())
              .filePath(QFileInfo(QCoreApplication::applicationFilePath()).fileName());
      QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), executable));
      auto environment = QProcessEnvironment::systemEnvironment();
      environment.insert("CPPCHAT_INSTALLED_PATH_PROBE", "1");
      environment.insert("QT_PLUGIN_PATH", QCoreApplication::applicationDirPath());
      QProcess probe;
      probe.setProcessEnvironment(environment);
      probe.start(executable, {"installedDirectoryStorage", "-o", "-,txt"});
      QVERIFY(probe.waitForFinished(20000));
      const QByteArray output = probe.readAllStandardOutput() + probe.readAllStandardError();
      QVERIFY2(probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0,
               output.constData());
      qInfo().noquote() << "Installed-directory probe passed:" << installation.path();
      return;
    }
    const auto restorePaths = qScopeGuard([&] {
      RuntimePaths::configure(root.path() + "/test.ini", root.path(), root.path() + "/logs");
    });
    RuntimePaths::configure({}, {}, root.path() + "/logs");
    const QString server = "installed-" + QUuid::createUuid().toString();
    auto result = awaitStore([&](auto done) { store->OpenAccount(101, server, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    const QString expectedRoot =
        QDir(QCoreApplication::applicationDirPath()).filePath("chat_data/accounts/");
    QVERIFY2(store->AccountDirectory().startsWith(expectedRoot),
             qPrintable(store->AccountDirectory()));
    const QString accountDirectory = store->AccountDirectory();
    QVERIFY(QFile::exists(accountDirectory + "/sql/db.sqlite"));
    QVERIFY(QDir(accountDirectory + "/attachments").exists());
    result = awaitStore([&](auto done) { store->SaveConversations({{7, 102, 0, "", ""}}, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    result =
        awaitStore([&](auto done) { store->SaveMessages({message(1, "installed")}, 7, 1, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    store->CloseAccount();
    result = awaitStore([&](auto done) { store->OpenAccount(101, server, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(store->AccountDirectory(), accountDirectory);
    result = awaitStore([&](auto done) { store->LoadMessages(7, 0, 10, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.messages.size(), 1);
    QCOMPARE(result.messages[0].content, QString("installed"));
  }
  void preferencesAccountIsolation()
  {
    ConversationPreferences::SavePinned(101, {7});
    ConversationPreferences::SaveUnread(101, {8});
    ConversationPreferences::Flush();
    QVERIFY(ConversationPreferences::Load(101).pinned.contains(7));
    QVERIFY(ConversationPreferences::Load(102).pinned.isEmpty());
    ConversationPreferences::RemoveThread(101, 7);
    ConversationPreferences::Flush();
    QVERIFY(!ConversationPreferences::Load(101).pinned.contains(7));
  }
  void screenshotScaling_data()
  {
    QTest::addColumn<double>("scale");
    for (double value : {1.0, 1.25, 1.5, 2.0})
      QTest::newRow(qPrintable(QString::number(value))) << value;
  }
  void screenshotScaling()
  {
    QFETCH(double, scale);
    const QSize pixels(qRound(800 * scale), qRound(600 * scale));
    QCOMPARE(
        CaptureGeometry::pixelRect({20, 20, 100, 80}, {800, 600}, pixels),
        QRect(qRound(20 * scale), qRound(20 * scale), qRound(100 * scale), qRound(80 * scale)));
    QCOMPARE(CaptureGeometry::pixelRect({120, 100, -100, -80}, {800, 600}, pixels),
             CaptureGeometry::pixelRect({20, 20, 100, 80}, {800, 600}, pixels));
    QVERIFY(CaptureGeometry::pixelRect({-20, -20, 40, 40}, {800, 600}, pixels).left() == 0);
    QVERIFY(CaptureGeometry::pixelRect({}, {}, pixels).isEmpty());
  }
  void resetFormValidationAndCancelSignal()
  {
    inspectedWidget = std::make_unique<ResetDialog>();
    auto &dialog = *static_cast<ResetDialog *>(inspectedWidget.get());
    dialog.show();
    auto *user = dialog.findChild<QLineEdit *>("user_edit");
    auto *confirm = dialog.findChild<QPushButton *>("confirm_btn");
    auto *cancel = dialog.findChild<QPushButton *>("cancel_btn");
    auto *error = dialog.findChild<QLabel *>("error_label");
    QVERIFY(user && confirm && cancel && error);
    QSignalSpy exit(&dialog, &ResetDialog::switchLogin);
    QTest::mouseClick(confirm, Qt::LeftButton);
    QVERIFY(!error->text().isEmpty());
    QTest::keyClicks(user, "TestUser");
    QCOMPARE(user->text(), QString("TestUser"));
    QTest::mouseClick(cancel, Qt::LeftButton);
    QCOMPARE(exit.size(), 1);
    QCOMPARE(user->text(), QString());
  }
  void loginEnterValidation_data()
  {
    QTest::addColumn<QString>("fieldName");
    QTest::addColumn<int>("key");
    QTest::newRow("email-return") << QString("email_edit") << int(Qt::Key_Return);
    QTest::newRow("email-enter") << QString("email_edit") << int(Qt::Key_Enter);
    QTest::newRow("password-return") << QString("pass_edit") << int(Qt::Key_Return);
    QTest::newRow("password-enter") << QString("pass_edit") << int(Qt::Key_Enter);
  }
  void loginEnterValidation()
  {
    QFETCH(QString, fieldName);
    QFETCH(int, key);
    inspectedWidget = std::make_unique<QStackedWidget>();
    auto *pages = static_cast<QStackedWidget *>(inspectedWidget.get());
    auto *dialog = new LoginDialog;
    pages->addWidget(dialog);
    pages->show();
    auto *field = dialog->findChild<QLineEdit *>(fieldName);
    auto *login = dialog->findChild<QPushButton *>("login_btn");
    auto *error = dialog->findChild<QLabel *>("err_tip");
    QVERIFY(field && login && error);
    QSignalSpy submissions(login, &QPushButton::clicked);
    field->setFocus();
    QTest::keyClick(field, Qt::Key(key));
    QCOMPARE(submissions.size(), 1);
    QCOMPARE(error->text(), InputValidator::emailError({}));
    QVERIFY(login->isEnabled());
    login->setEnabled(false);
    QTest::keyClick(field, Qt::Key(key));
    QCOMPARE(submissions.size(), 1);
  }
  void loginEnterSubmitsOnce()
  {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    const QString previousUrl = gate_url_prefix;
    const auto restoreUrl = qScopeGuard([&] { gate_url_prefix = previousUrl; });
    gate_url_prefix = QString("http://127.0.0.1:%1").arg(server.serverPort());
    inspectedWidget = std::make_unique<QStackedWidget>();
    auto *pages = static_cast<QStackedWidget *>(inspectedWidget.get());
    auto *dialog = new LoginDialog;
    pages->addWidget(dialog);
    pages->show();
    auto *email = dialog->findChild<QLineEdit *>("email_edit");
    auto *password = dialog->findChild<QLineEdit *>("pass_edit");
    auto *login = dialog->findChild<QPushButton *>("login_btn");
    QVERIFY(email && password && login);
    email->setText("user@example.invalid");
    password->setText("abc123456");
    QSignalSpy submissions(login, &QPushButton::clicked);
    QSignalSpy connections(&server, &QTcpServer::newConnection);
    password->setFocus();
    QTest::keyClick(password, Qt::Key_Return);
    QCOMPARE(submissions.size(), 1);
    QVERIFY(!login->isEnabled());
    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> connection(server.nextPendingConnection());
    QByteArray request;
    QTRY_VERIFY(([&] {
      request += connection->readAll();
      const auto offset = request.indexOf("\r\n\r\n");
      return offset >= 0 && QJsonDocument::fromJson(request.mid(offset + 4)).isObject();
    })());
    QVERIFY(request.startsWith("POST /user_login HTTP/1.1\r\n"));
    const auto body =
        QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4)).object();
    QCOMPARE(body.value("email").toString(), email->text());
    QCOMPARE(body.value("passwd").toString(), password->text());
    QTest::keyClick(password, Qt::Key_Enter);
    QCOMPARE(submissions.size(), 1);
    QCOMPARE(connections.size(), 1);
    const QByteArray response =
        QJsonDocument(QJsonObject{{"error", int(ErrorCodes::PasswdInvalid)}})
            .toJson(QJsonDocument::Compact);
    connection->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                      QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n" +
                      response);
    connection->disconnectFromHost();
    QTRY_VERIFY(login->isEnabled());
  }
  void placeholdersRemainLocal()
  {
    inspectedWidget = std::make_unique<FavoritesPage>();
    secondaryWidget = std::make_unique<DiscoverPage>();
    auto &favorites = *static_cast<FavoritesPage *>(inspectedWidget.get());
    auto &discover = *static_cast<DiscoverPage *>(secondaryWidget.get());
    favorites.show();
    discover.show();
    const QString directory = store->AccountDirectory();
    const auto before = awaitStore([&](auto done) { store->LoadMessages(7, 0, 50, done); });
    for (auto *button : favorites.findChildren<QPushButton *>())
      QTest::mouseClick(button, Qt::LeftButton);
    for (auto *button : discover.findChildren<QPushButton *>())
      QTest::mouseClick(button, Qt::LeftButton);
    discover.Reset();
    favorites.Reset();
    const auto after = awaitStore([&](auto done) { store->LoadMessages(7, 0, 50, done); });
    QCOMPARE(after.messages.size(), before.messages.size());
    QCOMPARE(store->AccountDirectory(), directory);
    QVERIFY(discover.findChild<QLabel *>("placeholder_description") != nullptr);
  }
};
QTEST_MAIN(ClientTests)
#include "client_tests.moc"
