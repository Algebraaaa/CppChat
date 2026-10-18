#include "app/thememanager.h"
#include "chatstorefixture.h"
#include "common/localstoresecurity.h"
#include "common/runtimepaths.h"
#include "dialogs/favoritespage.h"
#include "network/resourcemgr.h"
#include "services/messagesnapshotservice.h"
#include <QBuffer>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using QtFixtures::awaitStore;

class FavoritesTests : public QObject {
  Q_OBJECT
  QTemporaryDir root;
  ChatStore *store = ChatStore::instance();
  const QString server = "https://favorites.example.test";

  StoredFavorite value(int id, const QString &text = "private favorite") const
  {
    StoredFavorite result;
    result.threadId = 7;
    result.serverId = id;
    result.senderUid = 102;
    result.senderName = "同事";
    result.content = text;
    return result;
  }
  ChatStoreResult save(const QVector<StoredFavorite> &values)
  {
    return awaitStore([&](auto done) { store->SaveFavorites(values, done); });
  }
  ChatStoreResult list(const QString &query = {}, FavoriteCategory category = FavoriteCategory::All,
                       qint64 before = 0)
  {
    return awaitStore([&](auto done) { store->LoadFavorites(query, category, before, done); });
  }
  void sql(const QString &statement)
  {
    const QString name = "favorites-fixture";
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
private slots:
  void init()
  {
    QVERIFY(root.isValid());
    const QString directory = root.filePath(QString::fromLatin1(QTest::currentTestFunction()));
    QVERIFY(QDir().mkpath(directory));
    RuntimePaths::configure(directory + "/config.ini", directory, directory + "/logs");
    const auto result = awaitStore([&](auto done) { store->OpenAccount(101, server, done); });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    const auto conversations =
        awaitStore([&](auto done) { store->SaveConversations({{7, 102}}, done); });
    QVERIFY2(conversations.error.isEmpty(), qPrintable(conversations.error));
    QString error;
    QVERIFY2(ThemeManager::apply(*qApp, &error), qPrintable(error));
  }
  void cleanup() { store->CloseAccount(); }
  void cleanupTestCase() { store->Shutdown(); }

  void snapshotsSurviveChatDeletionRestartAndStayAccountIsolated()
  {
    StoredMessage message;
    message.threadId = 7;
    message.senderUid = 102;
    message.serverId = 1;
    message.uniqueId = "original-1";
    message.content = "private favorite";
    message.time = "2026-10-19 00:40:00";
    auto original = awaitStore([&](auto done) { store->SaveMessages({message}, 0, -1, done); });
    QVERIFY(original.error.isEmpty());
    auto result = save({value(1)});
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    result = awaitStore([&](auto done) { store->DeleteLocalMessage(7, 1, done); });
    QVERIFY(result.error.isEmpty());
    original = awaitStore([&](auto done) { store->LoadMessages(7, 0, 10, done); });
    QVERIFY(original.messages.isEmpty());
    result = awaitStore([&](auto done) { store->ClearLocalHistory(7, done); });
    QVERIFY(result.error.isEmpty());
    QCOMPARE(list().favorites.size(), 1);
    store->CloseAccount();
    result = awaitStore([&](auto done) { store->OpenAccount(101, server, done); });
    QVERIFY(result.error.isEmpty());
    QCOMPARE(list().favorites.first().content, QString("private favorite"));
    result = awaitStore([&](auto done) { store->OpenAccount(102, server, done); });
    QVERIFY(result.error.isEmpty());
    QVERIFY(list().favorites.isEmpty());
    result = awaitStore([&](auto done) { store->OpenAccount(101, server + "/other", done); });
    QVERIFY(result.error.isEmpty());
    QVERIFY(list().favorites.isEmpty());
  }

  void duplicateSearchWildcardsPaginationAndCategories()
  {
    QVERIFY(save({value(1, "100%_literal"), value(2, "https://example.test")}).error.isEmpty());
    QVERIFY(save({value(1, "replacement must not overwrite the snapshot")}).error.isEmpty());
    auto result = list("%_");
    QCOMPARE(result.favorites.size(), 1);
    QCOMPARE(result.favorites.first().content, QString("100%_literal"));
    QCOMPARE(list({}, FavoriteCategory::Links).favorites.size(), 1);
    QCOMPARE(list({}, FavoriteCategory::Recent).favorites.size(), 2);
    QVERIFY(list({}, FavoriteCategory::Audio).favorites.isEmpty());
    QVector<StoredFavorite> extra;
    for (int id = 3; id <= 52; ++id)
      extra.append(value(id));
    QVERIFY(save(extra).error.isEmpty());
    result = list();
    QCOMPARE(result.favorites.size(), 50);
    QVERIFY(result.more);
    const auto last = result.favorites.last().id;
    const auto next = list({}, FavoriteCategory::All, last);
    QCOMPARE(next.favorites.size(), 2);
    QVERIFY(!next.more);
    QVERIFY(next.favorites.first().id < last);
  }

  void mediaIsIndependentAndChecksumFailureIsDetected()
  {
    auto image = value(3, QString(32, 'a'));
    image.type = 1;
    QImage picture(16, 16, QImage::Format_RGB32);
    picture.fill(Qt::blue);
    QBuffer output(&image.media);
    QVERIFY(output.open(QIODevice::WriteOnly));
    QVERIFY(picture.save(&output, "PNG"));
    auto attachment =
        awaitStore([&](auto done) { store->SaveAttachment(image.content, image.media, done); });
    QVERIFY(attachment.error.isEmpty());
    QVERIFY(save({image}).error.isEmpty());
    auto result = list({}, FavoriteCategory::Images);
    QCOMPARE(result.favorites.size(), 1);
    QVERIFY(result.favorites.first().media.isEmpty()); // Lists never load all media into memory.
    const auto id = result.favorites.first().id;
    result = awaitStore([&](auto done) { store->GetFavorite(id, done); });
    QCOMPARE(result.favorites.first().media, image.media);
    sql("UPDATE favorites SET media_sha256='invalid'");
    result = awaitStore([&](auto done) { store->GetFavorite(id, done); });
    QVERIFY(!result.error.isEmpty());
    result = awaitStore([&](auto done) { store->RemoveFavorites({id}, done); });
    QVERIFY(result.error.isEmpty());
    attachment = awaitStore([&](auto done) { store->LoadAttachment(image.content, done); });
    QCOMPARE(attachment.image, image.media);
  }

  void transactionRollbackAndRemovalDoNotAffectMessages()
  {
    sql("CREATE TRIGGER reject_favorite BEFORE INSERT ON favorites "
        "WHEN NEW.content='reject' BEGIN SELECT RAISE(ABORT,'fixture rejection'); END");
    QVERIFY(!save({value(1), value(2, "reject")}).error.isEmpty());
    QVERIFY(list().favorites.isEmpty());
    QVERIFY(save({value(1)}).error.isEmpty());
    const auto id = list().favorites.first().id;
    const auto result = awaitStore([&](auto done) { store->RemoveFavorites({id}, done); });
    QVERIFY(result.error.isEmpty());
    QVERIFY(list().favorites.isEmpty());
    auto invalid = value(0);
    QVERIFY(!save({value(1), invalid}).error.isEmpty());
    QVERIFY(list().favorites.isEmpty());
  }

  void snapshotCollectionKeepsOrderRejectsPartialFailureAndCancelsOnAccountChange()
  {
    const QString file(32, 'f');
    const QByteArray bytes("independent attachment snapshot");
    auto result = awaitStore([&](auto done) { store->SaveAttachment(file, bytes, done); });
    QVERIFY(result.error.isEmpty());
    auto image = value(2, file);
    image.type = 1;
    MessageSnapshotService service;
    bool complete = false;
    QVector<StoredFavorite> output;
    QString failure;
    const auto callback = [&](QVector<StoredFavorite> values, QString error) {
      output = values;
      failure = error;
      complete = true;
    };
    QVERIFY(
        service.Collect({value(1, "text before"), image, image, value(3, "text after")}, callback));
    QVERIFY(!service.Collect({value(4)}, callback));
    QTRY_VERIFY(complete);
    QVERIFY(failure.isEmpty());
    QCOMPARE(output.size(), 4);
    QCOMPARE(output[0].content, QString("text before"));
    QCOMPARE(output[1].media, bytes);
    QCOMPARE(output[2].media, bytes);
    QCOMPARE(output[3].content, QString("text after"));
    complete = false;
    auto invalid = value(5);
    invalid.type = 999;
    QVERIFY(service.Collect({value(1), invalid}, callback));
    QTRY_VERIFY(complete);
    QVERIFY(!failure.isEmpty());
    QVERIFY(output.isEmpty());
    complete = false;
    QVERIFY(service.Collect({image}, callback));
    store->CloseAccount();
    QTest::qWait(20);
    QVERIFY(!complete);
    QVERIFY(!service.Busy());
  }

  void visiblePageRefreshesAfterAsynchronousAccountOpen()
  {
    QVERIFY(save({value(1, "restored favorite")}).error.isEmpty());
    store->CloseAccount();
    FavoritesPage page;
    page.show();
    auto *items = page.findChild<QListWidget *>("favoritesList");
    QCOMPARE(items->count(), 0);
    const auto result = awaitStore([&](auto done) { store->OpenAccount(101, server, done); });
    QVERIFY(result.error.isEmpty());
    QTRY_COMPARE(items->count(), 1);
    QVERIFY(items->item(0)->text().contains("restored favorite"));
  }

  void actualPageSearchPreviewAndAccountChangeClearVisibleData()
  {
    QVERIFY(save({value(1, "<b>plain favorite</b>"), value(2, "another")}).error.isEmpty());
    FavoritesPage page;
    page.resize(1000, 700);
    page.show();
    auto *items = page.findChild<QListWidget *>("favoritesList");
    auto *search = page.findChild<QLineEdit *>("favorites_search_edit");
    QVERIFY(items && search);
    QTRY_COMPARE(items->count(), 2);
    const auto artifacts = qEnvironmentVariable("CPPCHAT_TEST_ARTIFACTS");
    if (!artifacts.isEmpty()) {
      QVERIFY(QDir().mkpath(artifacts));
      QVERIFY(page.grab().save(artifacts + "/favorites-list.png"));
    }
    search->setText("plain favorite");
    QTRY_COMPARE(items->count(), 1);
    items->setCurrentRow(0);
    QSignalSpy resend(&page, &FavoritesPage::resendRequested);
    page.findChild<QPushButton *>("favoriteSend")->click();
    QTRY_COMPARE(resend.size(), 1);
    const auto snapshot = qvariant_cast<QVector<StoredFavorite>>(resend.first().first());
    QCOMPARE(snapshot.size(), 1);
    QCOMPARE(snapshot.first().content, QString("<b>plain favorite</b>"));
    page.findChild<QPushButton *>("favoriteOpen")->click();
    QTRY_VERIFY(page.findChild<QDialog *>("favoritePreview"));
    auto *text = page.findChild<QPlainTextEdit *>();
    QVERIFY(text);
    QCOMPARE(text->toPlainText(), QString("<b>plain favorite</b>"));
    const auto result = awaitStore([&](auto done) { store->OpenAccount(102, server, done); });
    QVERIFY(result.error.isEmpty());
    QCOMPARE(items->count(), 0);
    QVERIFY(!page.findChild<QDialog *>("favoritePreview") ||
            !page.findChild<QDialog *>("favoritePreview")->isVisible());
  }
};
QTEST_MAIN(FavoritesTests)
#include "favorites_tests.moc"
