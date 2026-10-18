#include "contactpreferences.h"
#include "runtimepaths.h"
#include <QCryptographicHash>
#include <QDir>
#include <QSettings>
#include <QUrl>
#include <algorithm>

namespace {
QString path() { return RuntimePaths::preferencesDirectory() + "/contacts.ini"; }
QString key(int account, const QString &server)
{
  const auto identity = QUrl(server).adjusted(QUrl::RemoveUserInfo | QUrl::RemoveQuery |
                                              QUrl::RemoveFragment | QUrl::StripTrailingSlash);
  const auto hash = QCryptographicHash::hash(identity.toEncoded(), QCryptographicHash::Sha256);
  return QString("servers/%1/accounts/%2/starred")
      .arg(QString::fromLatin1(hash.toHex()))
      .arg(account);
}
} // namespace

QSet<int> ContactPreferences::LoadStarred(int account, const QString &server)
{
  QSet<int> result;
  if (account <= 0 || server.isEmpty())
    return result;
  QSettings settings(path(), QSettings::IniFormat);
  for (const auto &value : settings.value(key(account, server)).toStringList()) {
    bool valid = false;
    const int uid = value.toInt(&valid);
    if (valid && uid > 0 && uid != account)
      result.insert(uid);
  }
  return result;
}

bool ContactPreferences::SaveStarred(int account, const QString &server, const QSet<int> &contacts)
{
  if (account <= 0 || server.isEmpty() || !QDir().mkpath(RuntimePaths::preferencesDirectory()))
    return false;
  auto ids = contacts.values();
  std::sort(ids.begin(), ids.end());
  QStringList values;
  for (int uid : ids)
    if (uid > 0 && uid != account)
      values.append(QString::number(uid));
  QSettings settings(path(), QSettings::IniFormat);
  const auto settingKey = key(account, server);
  const auto previous = settings.value(settingKey);
  settings.setValue(settingKey, values);
  settings.sync();
  if (settings.status() == QSettings::NoError)
    return true;
  // QSettings shares its in-process cache. A failed write must not become a
  // successful-looking value when a new settings object reads the same file.
  if (previous.isValid())
    settings.setValue(settingKey, previous);
  else
    settings.remove(settingKey);
  return false;
}
