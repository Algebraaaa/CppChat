#include "mobilemedia.h"
#include "common/fileattachment.h"
#include "common/runtimepaths.h"
#include "common/userdata.h"
#include "network/tcpmgr.h"
#include "services/voicemessageaudio.h"
#include <QCborMap>
#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class MobileMediaTests : public QObject
{
  Q_OBJECT
private slots:
  void arbitraryFileBytesRoundTripAndExplicitSave()
  {
    QByteArray bytes;
    for (int i = 0; i < 256; ++i)
      bytes += char(i);
    bytes += QByteArray(70000, '\0');
    const QString name = QString::fromUtf8("资料-测试.txt");
    const auto encoded = FileAttachment::Pack(name, bytes);
    QVERIFY(!encoded.isEmpty());
    const auto file = FileAttachment::Unpack(encoded);
    QVERIFY(file.valid);
    QCOMPARE(file.name, name);
    QCOMPARE(file.bytes, bytes);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString error;
    const auto path = directory.filePath(file.name);
    QVERIFY2(FileAttachment::Save(file, path, error), qPrintable(error));
    QFile saved(path);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), bytes);
    const auto empty = FileAttachment::Unpack(FileAttachment::Pack("empty.txt", {}));
    QVERIFY(empty.valid);
    QVERIFY(empty.bytes.isEmpty());
    TextChatData summary("uuid", 1, ChatFormType::PRIVATE, ChatMsgType::FILE, "opaque-id", 1, 0, "");
    QCOMPARE(summary.Summary(), QString::fromUtf8("[文件]"));
  }
  void malformedOrUnsafeFilesAreRejected()
  {
    for (const auto &name : {QString("../escape"), QString("C:\\escape.txt"), QString("a/b"),
                             QString("bad\nname"), QString(".."), QString("trailing."), QString(256, 'a')})
      QVERIFY(FileAttachment::Pack(name, "data").isEmpty());
    const auto encoded = FileAttachment::Pack("safe.bin", "data");
    QVERIFY(!FileAttachment::Unpack(encoded.left(encoded.size() - 1)).valid);
    QVERIFY(!FileAttachment::Unpack(encoded + "garbage").valid);
    QVERIFY(!FileAttachment::Unpack("not a file").valid);
    const QCborMap wrong{{QStringLiteral("v"), 2}, {QStringLiteral("name"), QStringLiteral("safe")},
                          {QStringLiteral("data"), QByteArray("data")}};
    QVERIFY(!FileAttachment::Unpack(QCborValue(wrong).toCbor()).valid);
    QVERIFY(FileAttachment::Pack("large.bin", QByteArray(FileAttachment::MaximumPayloadBytes + 1, 'x')).isEmpty());
  }
  void incomingVideoCancelAndDisconnectReleaseDeviceOwnership()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    RuntimePaths::configure(directory.filePath("config.ini"), directory.path(), directory.filePath("logs"));
    MobileMedia media;
    QSignalSpy changes(&media, &MobileMedia::changed);
    const QJsonObject invite{{"call_id", QString(32, 'a')}, {"peer", 123}, {"action", "invite"},
                             {"incoming", true}, {"video", true}, {"error", 0}};
    auto *tcp = TcpMgr::GetInstance().get();
    emit tcp->sig_voice_event(invite);
    QCOMPARE(media.call()["state"].toInt(), 2);
    QVERIFY(media.call()["video"].toBool());
    QVERIFY(VoiceMessageAudio::instance()->CallBusy());
    QVERIFY(!media.recording());
    QVERIFY(!media.permissionPending()); // Incoming invitations never request devices.
    media.endCall();
    QCOMPARE(media.call()["state"].toInt(), 0);
    QVERIFY(!VoiceMessageAudio::instance()->CallBusy());
    emit tcp->sig_voice_event(invite);
    emit tcp->sig_connection_closed();
    QCOMPARE(media.call()["state"].toInt(), 0);
    QVERIFY(!VoiceMessageAudio::instance()->CallBusy());
    QVERIFY(media.playing().isEmpty());
    QVERIFY(changes.count() >= 4);
  }
};
int main(int argc, char **argv)
{
  QGuiApplication app(argc, argv);
  MobileMediaTests tests;
  return QTest::qExec(&tests, argc, argv);
}
#include "mobile_media.moc"
