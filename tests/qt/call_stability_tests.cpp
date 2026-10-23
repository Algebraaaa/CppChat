#include "media/callpolicy.h"
#include "services/voicecallservice.h"
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

class CallStabilityTests : public QObject {
  Q_OBJECT
  QJsonObject event(const VoiceCallService &call, const QString &action, int error = 0)
  {
    return {{"call_id", call.callId()},
            {"peer", call.peer()},
            {"video", call.isVideo()},
            {"action", action},
            {"error", error}};
  }
private slots:
  void recoverableMediaErrorDoesNotEndAcceptedCall_data()
  {
    QTest::addColumn<bool>("video");
    QTest::addColumn<QString>("action");
    QTest::addColumn<int>("error");
    QTest::newRow("audio-late-frame") << false << QString("frame") << 1023;
    QTest::newRow("video-camera-switch-late-frame") << true << QString("video") << 1023;
    QTest::newRow("audio-burst-rate-limit") << false << QString("frame") << 1025;
    QTest::newRow("video-temporary-relay-unavailable") << true << QString("video") << 1024;
  }
  void recoverableMediaErrorDoesNotEndAcceptedCall()
  {
    QFETCH(bool, video);
    QFETCH(QString, action);
    QFETCH(int, error);
    VoiceCallService call;
    QSignalSpy ended(&call, &VoiceCallService::ended);
    QSignalSpy sent(&call, &VoiceCallService::send);
    QVERIFY(call.start(42, video));
    call.receive(event(call, "accept"));
    const auto id = call.callId();
    call.receive(event(call, action, error));
    QCOMPARE(call.state(), VoiceCallService::State::Active);
    QCOMPARE(call.callId(), id);
    QCOMPARE(ended.size(), 0);
    call.sendAudio("next-opus-frame");
    QCOMPARE(sent.last()[0].toJsonObject()["audio"].toString(),
             QString::fromLatin1(QByteArray("next-opus-frame").toBase64()));
    call.receive(event(call, "end"));
    QCOMPARE(call.state(), VoiceCallService::State::Idle);
  }
  void authenticationAndExplicitControlFailuresRemainTerminal_data()
  {
    QTest::addColumn<QString>("action");
    QTest::addColumn<int>("error");
    QTest::newRow("revoked-session") << QString("frame") << 1010;
    QTest::newRow("not-friends") << QString("ping") << 1017;
    QTest::newRow("expired-call") << QString("ping") << 1023;
  }
  void authenticationAndExplicitControlFailuresRemainTerminal()
  {
    QFETCH(QString, action);
    QFETCH(int, error);
    VoiceCallService call;
    QVERIFY(call.start(42));
    call.receive(event(call, "accept"));
    call.receive(event(call, action, error));
    QCOMPARE(call.state(), VoiceCallService::State::Idle);
  }
  void boundedInterruptionRecoversWithSameCallAndStillExpires()
  {
    qint64 now = 100;
    VoiceCallService call(nullptr, [&] { return now; });
    QSignalSpy ended(&call, &VoiceCallService::ended);
    QVERIFY(call.start(42, true));
    call.receive(event(call, "accept"));
    const auto id = call.callId();
    const auto delayedPong = event(call, "ping");
    for (auto *timer : call.findChildren<QTimer *>())
      if (timer->isActive())
        timer->setInterval(1);
    now += cppchat::media::CallPolicy::RecoveringMs;
    QTRY_VERIFY(call.recovering());
    QCOMPARE(call.state(), VoiceCallService::State::Active);
    call.receive(event(call, "ping", 1015)); // A temporary Redis error is not progress.
    QVERIFY(call.recovering());
    now += 9000;
    auto media = event(call, "frame");
    media["seq"] = 1;
    media["audio"] = "eA==";
    call.receiveAudio(media);
    QVERIFY(!call.recovering());
    QCOMPARE(call.callId(), id);
    QCOMPARE(ended.size(), 0);
    now += cppchat::media::CallPolicy::TimeoutMs - 1;
    call.receive(event(call, "video", 1023));
    QTest::qWait(5);
    QCOMPARE(call.state(), VoiceCallService::State::Active);
    now += 1;
    QTRY_COMPARE(call.state(), VoiceCallService::State::Idle);
    QCOMPARE(ended.size(), 1);
    call.receive(delayedPong);
    QCOMPARE(call.state(), VoiceCallService::State::Idle);
  }
  void peerRecoveryAndCameraStateAreReconciledByPong()
  {
    VoiceCallService call;
    QSignalSpy camera(&call, &VoiceCallService::peerCameraChanged);
    QSignalSpy sentVideo(&call, &VoiceCallService::sendVideoFrame);
    QVERIFY(call.start(42, true));
    call.receive(event(call, "accept"));
    auto pong = event(call, "ping");
    pong["peer_idle_ms"] = cppchat::media::CallPolicy::RecoveringMs;
    pong["peer_camera_enabled"] = false;
    call.receive(pong);
    QVERIFY(call.recovering());
    QCOMPARE(camera.size(), 1);
    call.sendVideo("jpeg");
    QCOMPARE(sentVideo.size(), 0); // Preserve voice/control while recovering.
    pong["peer_idle_ms"] = 0;
    pong["peer_camera_enabled"] = true;
    call.receive(pong);
    QVERIFY(!call.recovering());
    QCOMPARE(camera.size(), 2);
    call.sendVideo("jpeg");
    QCOMPARE(sentVideo.size(), 1);
    call.finish();
  }
  void malformedAndForeignTrafficCannotExtendCallLifetime()
  {
    qint64 now = 0;
    VoiceCallService call(nullptr, [&] { return now; });
    QVERIFY(call.start(42));
    call.receive(event(call, "accept"));
    for (auto *timer : call.findChildren<QTimer *>())
      if (timer->isActive())
        timer->setInterval(1);
    now = cppchat::media::CallPolicy::TimeoutMs;
    auto foreign = event(call, "ping");
    foreign["peer"] = 43;
    call.receive(foreign);
    auto invalid = event(call, "frame");
    invalid["seq"] = 1;
    invalid["audio"] = "!!!!";
    call.receiveAudio(invalid);
    QTRY_COMPARE(call.state(), VoiceCallService::State::Idle);
  }
};
QTEST_GUILESS_MAIN(CallStabilityTests)
#include "call_stability_tests.moc"
