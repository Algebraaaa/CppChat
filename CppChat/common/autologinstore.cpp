#include "autologinstore.h"
#include "inputvalidator.h"
#include "localdataprotection.h"
#include "runtimepaths.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>

namespace {
QByteArray origin(const QUrl &server)
{
  if (!server.isValid() || server.host().isEmpty() || !server.userInfo().isEmpty() ||
      (server.scheme() != "https" && server.scheme() != "http"))
    throw std::runtime_error("Invalid automatic login origin");
  QUrl value;
  value.setScheme(server.scheme());
  value.setHost(server.host());
  value.setPort(server.port(server.scheme() == "https" ? 443 : 80));
  return value.toEncoded();
}
QString directory() { return RuntimePaths::preferencesDirectory() + "/auto-login"; }
QString fileName(const QUrl &server)
{
  return directory() + '/' +
         QCryptographicHash::hash(origin(server), QCryptographicHash::Sha256).toHex() + ".bin";
}
QByteArray context(const QUrl &server)
{
  return QByteArray("CppChat auto login v1:") + origin(server);
}
} // namespace

bool AutoLoginStore::supported()
{
#ifdef Q_OS_WIN
  return true;
#else
  return false; // Never persist passwords through the non-Windows plaintext fallback.
#endif
}

std::optional<AutoLoginStore::Credentials> AutoLoginStore::load(const QUrl &server)
{
  if (!supported())
    return std::nullopt;
  QFile file(fileName(server));
  if (!file.exists())
    return std::nullopt;
  if (!file.open(QIODevice::ReadOnly) || file.size() > 16384)
    throw std::runtime_error("Cannot read automatic login credentials");
  auto bytes = LocalDataProtection::unprotect(file.readAll(), context(server));
  const auto object = QJsonDocument::fromJson(bytes).object();
  bytes.fill('\0');
  Credentials result{object["email"].toString(), object["password"].toString()};
  if (object["format"].toInt() != 1 || !InputValidator::emailError(result.email).isEmpty() ||
      !InputValidator::passwordError(result.password).isEmpty())
    throw std::runtime_error("Invalid automatic login credentials");
  return result;
}

void AutoLoginStore::save(const QUrl &server, const Credentials &credentials)
{
  if (!supported())
    throw std::runtime_error("Secure automatic login is unavailable on this platform");
  const auto path = fileName(server);
  LocalDataProtection::secureDirectory(directory());
  if (!InputValidator::emailError(credentials.email).isEmpty() ||
      !InputValidator::passwordError(credentials.password).isEmpty())
    throw std::runtime_error("Invalid automatic login credentials");
  auto bytes = QJsonDocument(QJsonObject{{"format", 1},
                                         {"email", credentials.email},
                                         {"password", credentials.password}})
                   .toJson(QJsonDocument::Compact);
  const auto encrypted = LocalDataProtection::protect(bytes, context(server));
  bytes.fill('\0');
  LocalDataProtection::atomicWrite(path, encrypted);
  LocalDataProtection::secureDirectory(directory(), true);
}

void AutoLoginStore::clear(const QUrl &server)
{
  const auto path = fileName(server);
  if (QFile::exists(path) && !QFile::remove(path))
    throw std::runtime_error("Cannot remove automatic login credentials");
}
