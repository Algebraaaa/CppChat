#include "appbridge.h"
#include "network/tcpmgr.h"
#include <QGuiApplication>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QDir>
#include <QTextStream>

int main(int argc, char **argv)
{
  QGuiApplication application(argc, argv);
  QCoreApplication::setOrganizationName("CppChatMobileTests");
  QCoreApplication::setApplicationName("UiSmoke");
  QQuickStyle::setStyle("Material");
  QFontDatabase::addApplicationFont(":/fonts/SourceHanSansCN-Regular.otf");
  application.setFont(QFont("Source Han Sans CN", 11));
  const auto output = qEnvironmentVariable("CPPCHAT_TEST_DATA");
  AppBridge bridge;
  bridge.init(output);
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("App", &bridge);
  bool warnings = false;
  QObject::connect(&engine, &QQmlEngine::warnings, &application,
                   [&](const auto &) { warnings = true; });
  engine.load(QUrl("qrc:/qml/main.qml"));
  if (engine.rootObjects().isEmpty())
    return 2;
  auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
  const QStringList pages{"LoginPage",      "RegisterPage", "ResetPage",
                          "SettingsPage",   "ChatListPage", "FriendPage",
                          "ChatDetailPage", "HistoryPage",  "GroupPage", "MediaMessages",
                          "IncomingVoiceCall", "IncomingVideoCall"};
  const QList<QSize> sizes{QSize(390, 780), QSize(320, 480), QSize(800, 480)};
  QQuickItem *current = nullptr;
  int step = -1;
  QDir().mkpath(output);
  QTimer timer;
  timer.setInterval(500);
  QObject::connect(&timer, &QTimer::timeout, &application, [&] {
    if (step >= 0) {
      const auto name = pages[step / sizes.size()];
      const auto size = sizes[step % sizes.size()];
      if (!window->grabWindow().save(output + '/' + name + '-' + QString::number(size.width()) +
                                     ".png"))
        warnings = true;
      delete current;
      current = nullptr;
      bridge.media()->endCall();
    }
    ++step;
    if (step == pages.size() * sizes.size()) {
      QTextStream(stdout) << "QML pages/screens=" << step << "; warnings=" << warnings << Qt::endl;
      application.exit(warnings ? 3 : 0);
      return;
    }
    const auto size = sizes[step % sizes.size()];
    window->resize(size);
    const auto name = pages[step / sizes.size()];
    if (name.startsWith("Incoming")) {
      emit TcpMgr::GetInstance()->sig_voice_event(
          QJsonObject{{"call_id", QString(32, 'a')}, {"peer", 123}, {"action", "invite"},
                       {"incoming", true}, {"video", name == "IncomingVideoCall"}, {"error", 0}});
      return;
    }
    QQmlComponent component(&engine, QUrl("qrc:/qml/pages/" + (name == "MediaMessages" ? "ChatDetailPage" : name) + ".qml"));
    current = qobject_cast<QQuickItem *>(component.createWithInitialProperties(
        {{"backend", QVariant::fromValue(&bridge)},
         {"navigation", QVariant::fromValue(window->findChild<QObject *>("navigation"))}}));
    if (!current) {
      qWarning() << component.errors();
      application.exit(4);
      return;
    }
    current->setParentItem(window->contentItem());
    current->setSize(QSizeF(size));
    current->setZ(100);
    if (name == "ChatDetailPage" || name == "MediaMessages") {
      QVariantList messages;
      for (int i = 1; i <= 4; ++i) {
        messages.append(QVariantMap{
            {"msgId", i == 4 ? 0 : i},
            {"uniqueId", QString::number(i)},
            {"threadId", 123},
            {"senderUid", 0},
            {"isSelf", i % 2 == 0},
            {"type", name == "MediaMessages" ? (i % 2 == 0 ? 2 : 3) : 0},
            {"status", i == 4 ? 1 : 2},
            {"time", "2026-10-23 00:00:00"},
            {"content",
             i == 1 ? QStringLiteral("Windows 发来的消息")
                    : (i == 2 ? QStringLiteral("窄屏聊天气泡应自动换行，输入栏固定在可用区域底部。")
                              : QStringLiteral("<b>消息按原文显示</b>\n支持换行与中文。"))}});
      }
      current->setProperty("messages", messages);
    }
  });
  timer.start();
  return application.exec();
}
