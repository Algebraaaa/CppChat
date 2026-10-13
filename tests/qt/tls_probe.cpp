#include "network/httpmgr.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTimer>

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  const auto args = app.arguments();
  if ((args.size() != 5 && args.size() != 6) || !QSslSocket::supportsSsl())
    return 2;
  QFile ca(args[2]);
  if (!ca.open(QIODevice::ReadOnly))
    return 2;
  QSslSocket socket;
  auto configuration = QSslConfiguration::defaultConfiguration();
  configuration.setCaCertificates(QSslCertificate::fromData(ca.readAll()));
  configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
  socket.setSslConfiguration(configuration);
  socket.setPeerVerifyName(args[3]);
  const bool expected = args[4] == "valid";
  if (args.size() == 6 && args[5] == "http") {
    QSslConfiguration::setDefaultConfiguration(configuration);
    auto manager = HttpMgr::GetInstance();
    QObject::connect(manager.get(), &HttpMgr::sig_login_mod_finish, &app,
                     [&](ReqId, const QByteArray &body, ErrorCodes error) {
                       const bool success =
                           error == ErrorCodes::Success && QJsonDocument::fromJson(body).isObject();
                       app.exit(success == expected ? 0 : 1);
                     });
    QTimer::singleShot(8000, &app, [&] { app.exit(2); });
    manager->PostHttpReq(QUrl("https://127.0.0.1:" + args[1] + "/user_login"),
                         {{"email", "probe@example.test"}, {"passwd", "wrong-password"}},
                         ReqId::ID_LOGIN_USER, Modules::LOGINMOD);
    return app.exec();
  }
  QObject::connect(&socket, &QSslSocket::encrypted, &app, [&] { app.exit(expected ? 0 : 1); });
  QObject::connect(&socket, &QSslSocket::sslErrors, &app, [&](const QList<QSslError> &errors) {
    // Rejected certificates are never ignored or retried with weaker validation.
    socket.abort();
    app.exit(!expected && !errors.isEmpty() ? 0 : 1);
  });
  QObject::connect(&socket, &QSslSocket::errorOccurred, &app, [&] {
    if (socket.error() != QAbstractSocket::SslHandshakeFailedError)
      app.exit(2);
  });
  QTimer::singleShot(8000, &app, [&] { app.exit(2); });
  socket.connectToHostEncrypted("127.0.0.1", args[1].toUShort());
  return app.exec();
}
