#include "app/thememanager.h"
#include "dialogs/voicecalldialog.h"
#include "services/videomedia.h"
#include "services/voicecallservice.h"
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

class VideoTests : public QObject {
  Q_OBJECT
  QJsonObject event(VoiceCallService &call, const QString &action)
  {
    return {{"call_id", call.callId()},
            {"peer", call.peer()},
            {"action", action},
            {"video", true},
            {"error", 0}};
  }
private slots:
  void codecBoundsAndMalformedImages()
  {
    QImage original(1280, 720, QImage::Format_RGB32);
    original.fill(Qt::red);
    const auto jpeg = VideoMedia::encodeFrame(original);
    QVERIFY(!jpeg.isEmpty());
    QVERIFY(jpeg.size() <= VideoMedia::MaxFrameBytes);
    const auto image = VideoMedia::decodeFrame(jpeg);
    QVERIFY(!image.isNull());
    QVERIFY(image.width() <= 320 && image.height() <= 240);
    QVERIFY(image.pixelColor(0, 0).red() > 240);
    QVERIFY(VideoMedia::decodeFrame({}).isNull());
    QVERIFY(VideoMedia::decodeFrame("invalid").isNull());
    QVERIFY(VideoMedia::decodeFrame(QByteArray(6001, 'x')).isNull());
    QByteArray oversized;
    QBuffer buffer(&oversized);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(original.save(&buffer, "JPEG", 5));
    QVERIFY(VideoMedia::decodeFrame(oversized).isNull());
    QVERIFY(VideoMedia::encodeFrame({}).isEmpty());
  }
  void consentCameraOffReplayAndDisconnect()
  {
    VoiceCallService call;
    QSignalSpy sent(&call, &VoiceCallService::sendVideoFrame);
    QSignalSpy received(&call, &VoiceCallService::videoFrame);
    QSignalSpy camera(&call, &VoiceCallService::peerCameraChanged);
    QVERIFY(call.start(42, true));
    call.sendVideo("before-consent");
    QCOMPARE(sent.size(), 0);
    call.receive(event(call, "accept"));
    QCOMPARE(call.state(), VoiceCallService::State::Active);
    QImage image(160, 120, QImage::Format_RGB32);
    image.fill(Qt::blue);
    const auto jpeg = VideoMedia::encodeFrame(image);
    call.sendVideo(jpeg);
    QCOMPARE(sent.size(), 1);
    auto frame = sent.last()[0].toJsonObject();
    call.receiveVideo(frame);
    call.receiveVideo(frame);
    QCOMPARE(received.size(), 1);
    call.setCameraEnabled(false);
    call.sendVideo(jpeg);
    QCOMPARE(sent.size(), 1);
    auto disabled = event(call, "camera");
    disabled["enabled"] = false;
    call.receive(disabled); // Own ACK must not change the peer's camera state.
    QCOMPARE(camera.size(), 0);
    disabled["incoming"] = true;
    call.receive(disabled);
    QCOMPARE(camera.size(), 1);
    frame["seq"] = 2;
    call.receiveVideo(frame);
    QCOMPARE(received.size(), 1);
    disabled["enabled"] = true;
    call.receive(disabled);
    call.receiveVideo(frame);
    QCOMPARE(received.size(), 2);
    frame["seq"] = 3;
    frame["call_id"] = QString(32, 'b');
    call.receiveVideo(frame);
    QCOMPARE(received.size(), 2);
    call.disconnected();
    call.receiveVideo(frame);
    QCOMPARE(received.size(), 2);
    QCOMPARE(call.state(), VoiceCallService::State::Idle);
  }
  void voiceCallCannotEscalateToVideo()
  {
    VoiceCallService call;
    QSignalSpy sent(&call, &VoiceCallService::sendVideoFrame);
    QVERIFY(call.start(42));
    auto answer = event(call, "accept");
    answer["video"] = false;
    call.receive(answer);
    call.sendVideo("forbidden");
    QCOMPARE(sent.size(), 0);
    call.finish();
    QVERIFY(call.start(42, true));
    answer = event(call, "accept");
    answer["video"] = false; // An older voice-only client did not consent to video.
    call.receive(answer);
    QCOMPARE(call.state(), VoiceCallService::State::Idle);
  }
  void incomingWindowDoesNotOpenDevices()
  {
    QString error;
    QVERIFY(ThemeManager::apply(*qobject_cast<QApplication *>(qApp), &error));
    VoiceCallDialog dialog;
    auto *call = dialog.findChild<VoiceCallService *>();
    auto *video = dialog.findChild<VideoMedia *>();
    QVERIFY(call && video);
    call->receive({{"call_id", QString(32, 'f')},
                   {"peer", 42},
                   {"action", "invite"},
                   {"error", 0},
                   {"incoming", true},
                   {"video", true}});
    QVERIFY(dialog.isVisible());
    QVERIFY(!video->capturing());
    QVERIFY(dialog.findChild<QLabel *>("localVideo")->isVisible());
    QVERIFY(!dialog.findChild<QPushButton *>("cameraButton")->isVisible());
    const auto directory = qEnvironmentVariable("CPPCHAT_VIDEO_ARTIFACTS");
    if (!directory.isEmpty()) {
      QDir().mkpath(directory);
      QVERIFY(dialog.grab().save(directory + "/incoming-video.png"));
    }
    QTest::keyClick(&dialog, Qt::Key_Escape);
    QCOMPARE(call->state(), VoiceCallService::State::Idle);
    QVERIFY(!video->capturing());
  }
};
QTEST_MAIN(VideoTests)
#include "video_tests.moc"
