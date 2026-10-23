#include "common/runtimepaths.h"
#include "network/tcpmgr.h"
#include "services/videomedia.h"
#include "services/voiceaudio.h"
#include "services/voicecallservice.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
#include <cstring>
#include <iostream>
#include <opus.h>

// A real Qt/TcpMgr client with synthetic audio; never opens a microphone.
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  if (argc != 2)
    return 2;
  QFile input(QString::fromLocal8Bit(argv[1]));
  if (!input.open(QIODevice::ReadOnly))
    return 2;
  const auto config = QJsonDocument::fromJson(input.readAll()).object();
  QTemporaryDir root;
  if (!root.isValid())
    return 2;
  QFile ini(root.filePath("config.ini"));
  if (!ini.open(QIODevice::WriteOnly))
    return 2;
  ini.write(config["tls"].toBool() ? "[ChatServer]\ntls=true\n" : "[ChatServer]\ntls=false\n");
  ini.close();
  if (config["tls"].toBool()) {
    auto ssl = QSslConfiguration::defaultConfiguration();
    auto authorities = ssl.caCertificates();
    authorities.append(QSslCertificate::fromPath(config["ca_file"].toString()));
    ssl.setCaCertificates(authorities);
    QSslConfiguration::setDefaultConfiguration(ssl);
  }
  RuntimePaths::configure(ini.fileName(), root.path(), root.filePath("logs"));
  int code;
  auto *encoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &code);
  auto *decoder = opus_decoder_create(48000, 1, &code);
  if (!encoder || !decoder)
    return 2;
  auto tcp = TcpMgr::GetInstance();
  VoiceCallService call;
  int sent = 0, received = 0, decoded = 0, videoDecoded = 0;
  const bool video = config["video"].toBool();
  bool ending = false;
  QElapsedTimer active, lastReceived;
  qint64 longestGap = 0;
  int recoveries = 0;
  QObject::connect(&call, &VoiceCallService::networkChanged, &call, [&](bool recovering) {
    if (recovering)
      ++recoveries;
  });
  QTimer media;
  media.setInterval(20);
  media.setTimerType(Qt::PreciseTimer);
  QObject::connect(&media, &QTimer::timeout, &call, [&] {
    QByteArray pcm(1920, '\0');
    // Includes an explicit silent interval, with the same encoded frame size.
    if (sent < 15 || sent >= 25) {
      for (int i = 0; i < 960; ++i) {
        const qint16 sample = qint16(8000 * std::sin((sent * 960 + i) * 0.0576));
        std::memcpy(pcm.data() + i * 2, &sample, 2);
      }
    }
    call.sendAudio(VoiceAudio::encodeFrame(encoder, pcm));
    ++sent;
    if (video && sent % 8 == 0) {
      QImage picture(320, 240, QImage::Format_RGB32);
      picture.fill(config["caller"].toBool() ? Qt::red : Qt::blue);
      call.sendVideo(VideoMedia::encodeFrame(picture));
    }
    if (video && sent == 32)
      call.setCameraEnabled(false);
    if (video && sent == 48)
      call.setCameraEnabled(true);
    if (config["caller"].toBool() && sent >= 60 && received >= 40 &&
        active.elapsed() >= config["minimum_active_ms"].toInt() && !call.recovering() &&
        (!video || videoDecoded >= 5) && !ending) {
      ending = true;
      call.finish();
    }
  });
  QObject::connect(&call, &VoiceCallService::send, tcp.get(),
                   [&](const QJsonObject &body, bool audio) {
                     tcp->SendVoice(body, audio);
                     if (audio && config["inject_replay"].toBool() && body["seq"].toInt() == 20)
                       tcp->SendVoice(body, true);
                   });
  QObject::connect(&call, &VoiceCallService::sendVideoFrame, tcp.get(), &TcpMgr::SendVideo);
  QObject::connect(tcp.get(), &TcpMgr::sig_video_image, &call, &VoiceCallService::receiveVideo);
  QObject::connect(&call, &VoiceCallService::videoFrame, &call, [&](const QByteArray &jpeg) {
    const auto picture = VideoMedia::decodeFrame(jpeg);
    if (!picture.isNull()) {
      const auto color = picture.pixelColor(0, 0);
      const bool correct = config["caller"].toBool() ? color.blue() > 220 : color.red() > 220;
      if (correct)
        ++videoDecoded;
    }
  });
  QObject::connect(tcp.get(), &TcpMgr::sig_voice_event, &call, &VoiceCallService::receive);
  QObject::connect(tcp.get(), &TcpMgr::sig_voice_audio, &call, &VoiceCallService::receiveAudio);
  QObject::connect(tcp.get(), &TcpMgr::sig_connection_closed, &call,
                   &VoiceCallService::disconnected);
  QObject::connect(&call, &VoiceCallService::changed, &call, [&] {
    if (call.state() == VoiceCallService::State::Incoming)
      call.accept();
    if (call.state() == VoiceCallService::State::Active) {
      if (!active.isValid())
        active.start();
      media.start();
    }
    if (call.state() == VoiceCallService::State::Idle)
      media.stop();
  });
  QObject::connect(&call, &VoiceCallService::audio, &call, [&](QByteArray packet, int) {
    if (lastReceived.isValid())
      longestGap = qMax(longestGap, lastReceived.elapsed());
    lastReceived.start();
    ++received;
    if (VoiceAudio::decodeFrame(decoder, packet).size() == 1920)
      ++decoded;
  });
  QObject::connect(&call, &VoiceCallService::ended, &call, [&](QString) {
    std::cout << QJsonDocument(QJsonObject{{"sent", sent},
                                           {"received", received},
                                           {"decoded", decoded},
                                           {"video_decoded", videoDecoded},
                                           {"recoveries", recoveries},
                                           {"longest_rx_gap_ms", longestGap},
                                           {"active_ms", active.isValid() ? active.elapsed() : 0}})
                     .toJson(QJsonDocument::Compact)
                     .constData()
              << std::endl;
    QTimer::singleShot(200, &app, [&] {
      app.exit(received >= 40 && decoded == received && (!video || videoDecoded >= 5) &&
                       active.elapsed() >= config["minimum_active_ms"].toInt() &&
                       longestGap >= config["expected_gap_ms"].toInt() &&
                       recoveries >= config["expected_recoveries"].toInt()
                   ? 0
                   : 1);
    });
  });
  QObject::connect(tcp.get(), &TcpMgr::sig_con_success, &call, [&](bool success) {
    if (!success) {
      app.exit(1);
      return;
    }
    tcp->slot_send_data(ID_CHAT_LOGIN,
                        QJsonDocument(QJsonObject{{"uid", config["uid"]},
                                                  {"token", config["token"]},
                                                  {"device", config["device"].toString("desktop")}})
                            .toJson(QJsonDocument::Compact));
  });
  QObject::connect(tcp.get(), &TcpMgr::sig_swich_chatdlg, &call, [&] {
    std::cout << "READY" << std::endl;
    if (!tcp->SupportsVoiceCall() || (video && !tcp->SupportsVideoCall())) {
      app.exit(1);
      return;
    }
    if (config["caller"].toBool())
      call.start(config["peer"].toInt(), video);
  });
  QObject::connect(tcp.get(), &TcpMgr::sig_login_failed, &app, [&] { app.exit(1); });
  QTimer::singleShot(config["timeout_ms"].toInt(17000), &app, [&] { app.exit(1); });
  ServerInfo server;
  server.Host = "127.0.0.1";
  server.Port = QString::number(config["port"].toInt());
  tcp->slot_tcp_connect(server);
  const int result = app.exec();
  call.finish();
  tcp->CloseConnection();
  opus_encoder_destroy(encoder);
  opus_decoder_destroy(decoder);
  return result;
}
