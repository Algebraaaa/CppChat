#include "app/mainwindow.h"
#include "app/thememanager.h"
#include "chatstorefixture.h"
#include "common/clientpreferences.h"
#include "common/runtimepaths.h"
#include "dialogs/chatdialog.h"
#include "dialogs/chathistorydialog.h"
#include "dialogs/chatpage.h"
#include "dialogs/clientsettingsdialog.h"
#include "dialogs/contactmanagerdialog.h"
#include "dialogs/conversationsection.h"
#include "dialogs/notesdialog.h"
#include "dialogs/remindersdialog.h"
#include "dialogs/updatedialog.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/clientupdateservice.h"
#include "widgets/MessageTextEdit.h"
#include "widgets/textbubble.h"
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>
#include <cmath>
#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <qt_windows.h>
#endif

using QtFixtures::awaitStore;
class ThemeTests : public QObject {
  Q_OBJECT
  QTemporaryDir root;
  int uid = 800;
  ChatStore *store = ChatStore::instance();
  std::shared_ptr<ChatThreadData> data;
  void apply(bool dark)
  {
    QString error;
    QVERIFY2(ThemeManager::apply(*qApp, &error,
                                 dark ? ThemeManager::Variant::Dark : ThemeManager::Variant::Light),
             qPrintable(error));
  }
  void preference(ClientPreferences::Appearance appearance)
  {
    auto state = ClientPreferences::instance()->Current();
    state.appearance = appearance;
    QString error;
    QVERIFY2(ClientPreferences::instance()->Save(state, error), qPrintable(error));
  }
  void snapshot(QWidget &widget, const QString &name)
  {
    const auto directory = qEnvironmentVariable("CPPCHAT_TEST_ARTIFACTS");
    if (directory.isEmpty())
      return;
    QTest::qWait(80);
    QVERIFY(QDir().mkpath(directory));
    QVERIFY(widget.grab().save(directory + '/' + name + ".png"));
  }
  double contrast(QColor a, QColor b)
  {
    const auto luminance = [](QColor c) {
      const auto f = [](double x) {
        return x <= .04045 ? x / 12.92 : std::pow((x + .055) / 1.055, 2.4);
      };
      return .2126 * f(c.redF()) + .7152 * f(c.greenF()) + .0722 * f(c.blueF());
    };
    double x = luminance(a), y = luminance(b);
    return (qMax(x, y) + .05) / (qMin(x, y) + .05);
  }
private slots:
  void initTestCase()
  {
    QVERIFY(root.isValid());
    RuntimePaths::configure(root.filePath("config.ini"), root.path(), root.filePath("logs"));
  }
  void init()
  {
    delete qApp->findChild<QObject *>("cppchat_theme_policy", Qt::FindDirectChildrenOnly);
    QString error;
    QVERIFY(ClientPreferences::instance()->Save({}, error));
    apply(false);
    gate_url_prefix = "https://theme.example.test";
    TcpMgr::GetInstance()->CloseConnection();
    auto mgr = UserMgr::GetInstance();
    mgr->Reset();
    mgr->SetUserInfo(std::make_shared<UserInfo>(++uid, "self", ""));
    auto friendInfo = std::make_shared<UserInfo>(102, "朋友", "小伙伴", "", 0);
    friendInfo->_back = "同事";
    friendInfo->_tags = {"工作"};
    mgr->AddFriend(friendInfo);
    data = std::make_shared<ChatThreadData>(102, 7, 0);
    mgr->AddChatThreadData(data, 102);
    auto result = awaitStore([&](auto done) { store->OpenAccount(uid, gate_url_prefix, done); });
    QVERIFY(result.error.isEmpty());
    result = awaitStore([&](auto done) { store->SaveConversations({{7, 102}}, done); });
    QVERIFY(result.error.isEmpty());
    StoredMessage message;
    message.threadId = 7;
    message.senderUid = 102;
    message.serverId = 3;
    message.uniqueId = "theme-message";
    message.content = "中文消息 English 🙂";
    message.time = "2026-10-19 16:00:00";
    result = awaitStore([&](auto done) { store->SaveMessages({message}, 0, -1, done); });
    QVERIFY(result.error.isEmpty());
    data->MoveMsg(message.ToMessage());
  }
  void cleanup()
  {
    delete qApp->findChild<QObject *>("cppchat_theme_policy", Qt::FindDirectChildrenOnly);
    store->CloseAccount();
    UserMgr::GetInstance()->Reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    apply(false);
  }
  void cleanupTestCase() { store->Shutdown(); }
  void darkTokensLoadAndReadablePaletteIsComplete()
  {
    UiTheme dark, light;
    QString error;
    QVERIFY(ThemeManager::load(":/style/theme.json", &light, &error));
    QVERIFY(ThemeManager::load(":/style/theme.json", &dark, &error, ThemeManager::Variant::Dark));
    QVERIFY(dark.dark);
    QVERIFY(!light.dark);
    QVERIFY(!dark.stylesheet.contains("${"));
    QVERIFY(dark.palette.color(QPalette::Window).lightness() < 80);
    QVERIFY(dark.palette.color(QPalette::Base).lightness() < 80);
    for (auto group : {QPalette::Active, QPalette::Inactive}) {
      QVERIFY(contrast(dark.palette.color(group, QPalette::Text),
                       dark.palette.color(group, QPalette::Base)) >= 4.5);
      QVERIFY(contrast(dark.palette.color(group, QPalette::HighlightedText),
                       dark.palette.color(group, QPalette::Highlight)) >= 4.5);
      QVERIFY(contrast(dark.palette.color(group, QPalette::PlaceholderText),
                       dark.palette.color(group, QPalette::Base)) >= 4.5);
    }
    const auto previous = dark.stylesheet;
    QVERIFY(!ThemeManager::load(root.filePath("missing.json"), &dark, &error,
                                ThemeManager::Variant::Dark));
    QCOMPARE(dark.stylesheet, previous);
  }
  void olderPreferencesDefaultToSystemAndManualModePersists()
  {
    const auto path = root.filePath("appearance.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{\"schema\":1,\"sendKey\":\"enter\",\"checkUpdatesOnStartup\":true}");
    file.close();
    ClientPreferences old(path);
    QCOMPARE(old.Current().appearance, ClientPreferences::Appearance::System);
    auto state = old.Current();
    state.appearance = ClientPreferences::Appearance::Dark;
    QString error;
    QVERIFY(old.Save(state, error));
    ClientPreferences loaded(path);
    QCOMPARE(loaded.Current().appearance, ClientPreferences::Appearance::Dark);
    state.appearance = static_cast<ClientPreferences::Appearance>(99);
    QVERIFY(!loaded.Save(state, error));
    QCOMPARE(loaded.Current().appearance, ClientPreferences::Appearance::Dark);
  }
  void liveSwitchPreservesEditorCursorAndUpdatesExistingBubbles()
  {
    ConversationSection section;
    section.resize(1050, 700);
    section.LoadLocalState();
    section.AddThreads({}, 10);
    section.SelectThread(data);
    section.show();
    auto *edit = section.Chat()->findChild<MessageTextEdit *>("chatEdit");
    QTRY_VERIFY(edit->isEnabled());
    edit->setPlainText("未发送草稿 中文🙂");
    auto cursor = edit->textCursor();
    cursor.setPosition(2);
    cursor.setPosition(6, QTextCursor::KeepAnchor);
    edit->setTextCursor(cursor);
    const auto selected = edit->textCursor().selectedText();
    for (bool dark : {true, false, true}) {
      apply(dark);
      QCoreApplication::processEvents();
      QCOMPARE(edit->toPlainText(), QString("未发送草稿 中文🙂"));
      QCOMPARE(edit->textCursor().selectedText(), selected);
      QVERIFY((edit->palette().color(QPalette::Text).lightness() > 180) == dark);
      auto bubbles = section.Chat()->findChildren<TextBubble *>();
      QVERIFY(!bubbles.isEmpty());
      for (auto *bubble : bubbles) {
        bubble->ensurePolished();
        auto *body = bubble->findChild<QTextEdit *>("chat_message_text");
        QVERIFY(contrast(body->palette().color(QPalette::Text), bubble->backgroundColor()) >= 4.5);
      }
    }
    snapshot(section, "dark-chat");
    section.OpenChatWindow();
    snapshot(*section.Chat()->window(), "dark-detached-chat");
    section.DockChat();
    apply(false);
    snapshot(section, "light-chat");
  }
  void systemSignalsAndManualOverrideApplyWithoutFeedback()
  {
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
    QSKIP("Colour scheme signal requires Qt 6.5; Qt 6.4 has a palette/registry fallback");
#else
    QString error;
    QVERIFY(ThemeManager::followSystem(*qApp, &error));
    auto *hints = qApp->styleHints();
    emit hints->colorSchemeChanged(Qt::ColorScheme::Dark);
    QTRY_VERIFY(qApp->property("cppchat_theme_dark").toBool());
    preference(ClientPreferences::Appearance::Light);
    QTRY_VERIFY(!qApp->property("cppchat_theme_dark").toBool());
    emit hints->colorSchemeChanged(Qt::ColorScheme::Dark);
    QTest::qWait(40);
    QVERIFY(!qApp->property("cppchat_theme_dark").toBool());
    preference(ClientPreferences::Appearance::System);
    QTRY_VERIFY(qApp->property("cppchat_theme_dark").toBool());
    emit hints->colorSchemeChanged(Qt::ColorScheme::Light);
    QTRY_VERIFY(!qApp->property("cppchat_theme_dark").toBool());
    preference(ClientPreferences::Appearance::Dark);
    QTRY_VERIFY(qApp->property("cppchat_theme_dark").toBool());
    emit hints->colorSchemeChanged(Qt::ColorScheme::Light);
    QTest::qWait(40);
    QVERIFY(qApp->property("cppchat_theme_dark").toBool());
    QTest::qWait(60);
    QVERIFY(qApp->property("cppchat_theme_dark").toBool());
#endif
  }
  void repeatedSwitchesDoNotRegisterMoreFontsOrWatchers()
  {
    const auto count = [] {
      int n = 0;
      for (int id = 0; id < 128; ++id)
        if (!QFontDatabase::applicationFontFamilies(id).isEmpty())
          ++n;
      return n;
    };
    const int before = count();
    for (int i = 0; i < 6; ++i)
      apply(i % 2 == 0);
    QCOMPARE(count(), before);
    QString error;
    QVERIFY(ThemeManager::followSystem(*qApp, &error));
    QVERIFY(ThemeManager::followSystem(*qApp, &error));
    QCOMPARE(
        qApp->findChildren<QObject *>("cppchat_theme_policy", Qt::FindDirectChildrenOnly).size(),
        1);
    QCOMPARE(count(), before);
  }
  void settingsAndSecondaryWindowsRecolourInPlace()
  {
    preference(ClientPreferences::Appearance::Light);
    QString error;
    QVERIFY(ThemeManager::followSystem(*qApp, &error));
    ClientSettingsDialog settings;
    NotesDialog notes;
    ContactManagerDialog contacts;
    ChatHistoryDialog history;
    RemindersDialog reminders;
    ClientUpdateService updates({}, "0.1.16");
    UpdateDialog update(&updates);
    settings.show();
    notes.show();
    contacts.show();
    history.SetAllConversations();
    history.show();
    reminders.show();
    update.show();
    auto *body = notes.findChild<QPlainTextEdit *>("noteBody");
    body->setPlainText("这段笔记切换后保留");
    auto *mode = settings.findChild<QComboBox *>("appearanceSetting");
    mode->setCurrentIndex(mode->findData(int(ClientPreferences::Appearance::Dark)));
    settings.findChild<QPushButton *>("saveSettings")->click();
    QTRY_VERIFY(qApp->property("cppchat_theme_dark").toBool());
    QCOMPARE(body->toPlainText(), QString("这段笔记切换后保留"));
    QVERIFY(body->palette().color(QPalette::Base).lightness() < 80);
    QVERIFY(contrast(body->palette().color(QPalette::Text),
                     body->palette().color(QPalette::Base)) >= 4.5);
    auto *table = contacts.findChild<QTableView *>();
    QVERIFY(table->palette().color(QPalette::Base).lightness() < 80);
    QTRY_COMPARE(history.findChild<QListWidget *>("results_list")->count(), 1);
    snapshot(settings, "dark-settings");
    snapshot(history, "dark-history");
    snapshot(contacts, "dark-contacts");
    snapshot(notes, "dark-notes");
    snapshot(update, "dark-update");
    mode->setCurrentIndex(mode->findData(int(ClientPreferences::Appearance::Light)));
    settings.findChild<QPushButton *>("saveSettings")->click();
    QTRY_VERIFY(!qApp->property("cppchat_theme_dark").toBool());
    QVERIFY(body->palette().color(QPalette::Base).lightness() > 220);
  }
  void windowsNativeCaptionsUseDarkDwmAttributes()
  {
#ifdef Q_OS_WIN
    if (qApp->platformName() != "windows")
      QSKIP("Run the additional QT_QPA_PLATFORM=windows validation for native DWM");
    QDialog native;
    native.setWindowTitle("CppChat theme frame validation");
    native.resize(420, 180);
    native.show();
    QVERIFY(QTest::qWaitForWindowExposed(&native));
    for (bool dark : {true, false, true}) {
      apply(dark);
      QTest::qWait(80);
      BOOL enabled = FALSE;
      const auto hwnd = reinterpret_cast<HWND>(native.winId());
      auto result = DwmGetWindowAttribute(hwnd, 20, &enabled, sizeof(enabled));
      if (FAILED(result))
        result = DwmGetWindowAttribute(hwnd, 19, &enabled, sizeof(enabled));
      if (SUCCEEDED(result))
        QCOMPARE(bool(enabled), dark);
      else
        QSKIP("DWM dark attribute unavailable on this Windows build");
      COLORREF caption = 0;
      if (SUCCEEDED(DwmGetWindowAttribute(hwnd, 35, &caption, sizeof(caption)))) {
        UiTheme theme;
        QString error;
        QVERIFY(
            ThemeManager::load(":/style/theme.json", &theme, &error,
                               dark ? ThemeManager::Variant::Dark : ThemeManager::Variant::Light));
        QCOMPARE(caption, RGB(theme.windowCaptionColor.red(), theme.windowCaptionColor.green(),
                              theme.windowCaptionColor.blue()));
      }
    }
#else
    QSKIP("Windows-only native frame check");
#endif
  }
  void realMainWindowAuthenticationPagesUseDarkTheme()
  {
    apply(true);
    MainWindow window;
    window.show();
    snapshot(window, "dark-login-main");
    auto *registerButton = window.findChild<QPushButton *>("reg_btn");
    QVERIFY(registerButton);
    registerButton->click();
    snapshot(window, "dark-register-main");
    auto *registerPage = window.findChild<QWidget *>("RegisterDialog");
    QVERIFY(registerPage);
    for (auto *label : registerPage->findChildren<QLabel *>())
      if (!label->text().isEmpty()) {
        label->ensurePolished();
        QVERIFY(label->palette().color(QPalette::WindowText).lightness() > 110);
      }
  }
};
QTEST_MAIN(ThemeTests)
#include "theme_tests.moc"
