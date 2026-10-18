#include "app/thememanager.h"
#include "dialogs/voicecalldialog.h"
#include "services/voiceaudio.h"
#include "services/voicecallservice.h"
#include <QApplication>
#include <QDir>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <cmath>
#include <cstring>
#include <opus.h>

class VoiceTests : public QObject {
  Q_OBJECT
  QJsonObject event(VoiceCallService &call, QString action, int error = 0)
  {
    return {
        {"call_id", call.callId()}, {"peer", call.peer()}, {"action", action}, {"error", error}};
  }
private slots:
  void initTestCase()
  {
    QString error;
    QVERIFY2(
        ThemeManager::apply(*qobject_cast<QApplication *>(QCoreApplication::instance()), &error),
        qPrintable(error));
  }
  void incomingWindowAndClose()
  {
    VoiceCallDialog dialog;
    auto *service = dialog.findChild<VoiceCallService *>();
    QVERIFY(service);
    service->receive(QJsonObject{{"call_id", QString(32, 'c')},
                                 {"peer", 15},
                                 {"action", "invite"},
                                 {"error", 0},
                                 {"incoming", true}});
    QVERIFY(dialog.isVisible());
    QVERIFY(dialog.findChild<QPushButton *>("voice_accept")->isVisible());
    QVERIFY(!dialog.findChild<QPushButton *>("voice_mute")->isVisible());
    const auto artifacts = qEnvironmentVariable("CPPCHAT_VOICE_ARTIFACTS");
    if (!artifacts.isEmpty()) {
      QDir().mkpath(artifacts);
      QVERIFY(dialog.grab().save(artifacts + "/incoming-call.png"));
    }
    QTest::keyClick(&dialog, Qt::Key_Escape);
    QCOMPARE(service->state(), VoiceCallService::State::Idle);
  }
  void outgoingAcceptMutePayloadAndHangup()
  {
    VoiceCallService call;
    QSignalSpy sent(&call, &VoiceCallService::send), audio(&call, &VoiceCallService::audio);
    QVERIFY(call.start(12));
    QVERIFY(!call.start(13));
    call.sendAudio("before-consent");
    QCOMPARE(sent.size(), 1);
    auto acceptance = event(call, "accept");
    call.receive(acceptance);
    QCOMPARE(call.state(), VoiceCallService::State::Active);
    call.sendAudio("opus-frame");
    QCOMPARE(sent.size(), 2);
    auto frame = event(call, "frame");
    frame["seq"] = 1;
    frame["audio"] = QString::fromLatin1(QByteArray("frame").toBase64());
    call.receiveAudio(frame);
    call.receiveAudio(frame);
    QCOMPARE(audio.size(), 1);
    call.finish();
    QCOMPARE(call.state(), VoiceCallService::State::Idle);
    call.receive(acceptance);
    QCOMPARE(call.state(), VoiceCallService::State::Idle);
  }
  void incomingRejectAndAccept()
  {
    VoiceCallService call;
    QSignalSpy sent(&call, &VoiceCallService::send);
    QJsonObject invitation{{"call_id", QString(32, 'a')},
                           {"peer", 20},
                           {"action", "invite"},
                           {"error", 0},
                           {"incoming", true}};
    call.receive(invitation);
    QCOMPARE(call.state(), VoiceCallService::State::Incoming);
    call.finish();
    QCOMPARE(sent.last()[0].toJsonObject()["action"].toString(), QString("reject"));
    invitation["call_id"] = QString(32, 'b');
    call.receive(invitation);
    call.accept();
    QCOMPARE(call.state(), VoiceCallService::State::Connecting);
    call.receive(event(call, "accept"));
    QCOMPARE(call.state(), VoiceCallService::State::Active);
    call.disconnected();
    QCOMPARE(call.state(), VoiceCallService::State::Idle);
  }
  void foreignStaleAndMalformedFrames()
  {
    VoiceCallService call;
    QSignalSpy audio(&call, &VoiceCallService::audio);
    QVERIFY(call.start(30));
    auto answer = event(call, "accept");
    answer["peer"] = 31;
    call.receive(answer);
    QCOMPARE(call.state(), VoiceCallService::State::Outgoing);
    answer["peer"] = 30;
    call.receive(answer);
    auto frame = event(call, "frame");
    frame["seq"] = 2;
    frame["audio"] = "!!!!";
    call.receiveAudio(frame);
    frame["audio"] = QString(700, 'a');
    call.receiveAudio(frame);
    frame["audio"] = "eA==";
    frame["call_id"] = QString(32, 'b');
    call.receiveAudio(frame);
    QCOMPARE(audio.size(), 0);
    call.finish();
  }
  void synchronousFailureDoesNotRecurse()
  {
    VoiceCallService call;
    int sends = 0;
    connect(&call, &VoiceCallService::send, &call, [&](QJsonObject body, bool) {
      ++sends;
      body["error"] = 1024;
      call.receive(body);
    });
    QVERIFY(call.start(1));
    QCOMPARE(call.state(), VoiceCallService::State::Idle);
    QCOMPARE(sends, 2);
  }
  void cancelIgnoresDelayedInviteAck()
  {
    VoiceCallService call;
    QVERIFY(call.start(30));
    const auto ack = event(call, "invite");
    call.finish();
    call.receive(ack);
    QCOMPARE(call.state(), VoiceCallService::State::Idle);
  }
  void opusRoundtripAndConcealment()
  {
    int code;
    auto *encoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &code);
    auto *decoder = opus_decoder_create(48000, 1, &code);
    QVERIFY(encoder && decoder);
    QByteArray pcm(1920, Qt::Uninitialized);
    for (int i = 0; i < 960; ++i) {
      qint16 sample = qint16(10000 * std::sin(i * 2 * 3.141592653589793 * 440 / 48000));
      std::memcpy(pcm.data() + i * 2, &sample, 2);
    }
    const auto packet = VoiceAudio::encodeFrame(encoder, pcm);
    QVERIFY(!packet.isEmpty());
    QVERIFY(packet.size() <= 512);
    const auto decoded = VoiceAudio::decodeFrame(decoder, packet);
    QCOMPARE(decoded.size(), 1920);
    QVERIFY(decoded != QByteArray(1920, '\0'));
    QCOMPARE(VoiceAudio::decodeFrame(decoder, {}).size(), 1920);
    QVERIFY(VoiceAudio::decodeFrame(decoder, QByteArray(513, 'x')).isEmpty());
    QVERIFY(VoiceAudio::decodeFrame(decoder, QByteArray(1, char(0xff))).isEmpty());
    QVERIFY(VoiceAudio::encodeFrame(encoder, QByteArray(2, '\0')).isEmpty());
    opus_encoder_destroy(encoder);
    opus_decoder_destroy(decoder);
  }
};
QTEST_MAIN(VoiceTests)
#include "voice_tests.moc"
