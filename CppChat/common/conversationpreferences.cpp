#include "conversationpreferences.h"
#include "global.h"
#include "runtimepaths.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QHash>
#include <QLoggingCategory>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <algorithm>

namespace {
QString settingsPath()
{
  const QString directory = RuntimePaths::preferencesDirectory();
  QDir().mkpath(directory);
  return directory + "/conversations.ini";
}

QSet<int> readThreads(const QVariant &value)
{
  QSet<int> result;
  for (const auto &entry : value.toStringList()) {
    bool valid = false;
    const int id = entry.toInt(&valid);
    if (valid && id > 0)
      result.insert(id);
  }
  return result;
}

// 所有入口在 GUI 线程调用；缓冲值先放在内存，定时器到期才交给 QSettings。
// 不能逐次 setValue 再延迟 sync：QSettings 自身也会在事件循环中自动同步。
class PreferenceStore : public QObject {
public:
  explicit PreferenceStore(QObject *parent)
      : QObject(parent), settings(settingsPath(), QSettings::IniFormat)
  {
    timer.setSingleShot(true);
    timer.setInterval(500);
    connect(&timer, &QTimer::timeout, this, [this] { Flush(); });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
            [this] { Flush(); });
  }
  ~PreferenceStore() override { Flush(); }

  QVariant Read(const QString &key) const
  {
    return pending.contains(key) ? QVariant(pending.value(key)) : settings.value(key);
  }

  void Save(const QString &key, const QStringList &values)
  {
    if (Read(key).toStringList() == values)
      return;
    pending.insert(key, values);
    // 固定窗口，持续来消息也不会让保存时间无限后移。
    if (!timer.isActive())
      timer.start(500);
  }

  void Flush()
  {
    timer.stop();
    if (pending.isEmpty())
      return;
    // 每批重新创建写入对象，避免上一次失败的 status 粘在后续重试上。
    QSettings writer(settings.fileName(), QSettings::IniFormat);
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
      writer.setValue(it.key(), it.value());
    writer.sync();
    if (writer.status() == QSettings::NoError) {
      pending.clear();
    } else {
      qWarning() << "Failed to save local conversation preferences";
      // 保留最新状态，下一次读取或保存仍能看到它；降低失败重试频率。
      timer.start(5000);
    }
  }

private:
  QSettings settings;
  QTimer timer;
  QHash<QString, QStringList> pending;
};

QPointer<PreferenceStore> preferenceStore;

PreferenceStore *store()
{
  Q_ASSERT(QCoreApplication::instance() &&
           QThread::currentThread() == QCoreApplication::instance()->thread());
  if (!preferenceStore)
    preferenceStore = new PreferenceStore(QCoreApplication::instance());
  return preferenceStore.data();
}

QString accountKey(int uid, const QString &key)
{
  return QString("accounts/%1/%2").arg(uid).arg(key);
}

QString scopedKey(int uid, const QString &name)
{
  const auto identity = QUrl(gate_url_prefix)
                            .adjusted(QUrl::RemoveUserInfo | QUrl::RemoveQuery |
                                      QUrl::RemoveFragment | QUrl::StripTrailingSlash);
  const auto hash = QCryptographicHash::hash(identity.toEncoded(), QCryptographicHash::Sha256);
  return QString("servers/%1/accounts/%2/%3")
      .arg(QString::fromLatin1(hash.toHex()))
      .arg(uid)
      .arg(name);
}

bool saveScoped(int uid, const QString &name, const QStringList &values)
{
  if (uid <= 0 || gate_url_prefix.isEmpty() || !QDir().mkpath(RuntimePaths::preferencesDirectory()))
    return false;
  QSettings settings(settingsPath(), QSettings::IniFormat);
  const auto key = scopedKey(uid, name);
  const auto previous = settings.value(key);
  settings.setValue(key, values);
  settings.sync();
  if (settings.status() == QSettings::NoError)
    return true;
  if (previous.isValid())
    settings.setValue(key, previous);
  else
    settings.remove(key);
  return false;
}

void saveThreads(int uid, const QString &key, const QSet<int> &threads)
{
  if (uid <= 0)
    return;
  auto ids = threads.values();
  std::sort(ids.begin(), ids.end());
  QStringList values;
  for (int id : ids)
    if (id > 0)
      values.append(QString::number(id));
  store()->Save(accountKey(uid, key), values);
}
} // namespace

ConversationPreferences::State ConversationPreferences::Load(int uid)
{
  if (uid <= 0)
    return {};
  auto *preferences = store();
  State state;
  state.pinned = readThreads(preferences->Read(accountKey(uid, "pinned")));
  state.unread = readThreads(preferences->Read(accountKey(uid, "unread")));
  QSettings settings(settingsPath(), QSettings::IniFormat);
  state.muted = readThreads(settings.value(scopedKey(uid, "muted")));
  for (const auto &value : settings.value(scopedKey(uid, "hidden")).toStringList()) {
    const auto parts = value.split(':');
    if (parts.size() != 2)
      continue;
    bool validId = false, validBoundary = false;
    const int id = parts[0].toInt(&validId), boundary = parts[1].toInt(&validBoundary);
    if (validId && validBoundary && id > 0 && boundary >= 0)
      state.hidden.insert(id, boundary);
  }
  return state;
}

void ConversationPreferences::SavePinned(int uid, const QSet<int> &threads)
{
  saveThreads(uid, "pinned", threads);
  Flush();
}

void ConversationPreferences::SaveUnread(int uid, const QSet<int> &threads)
{
  saveThreads(uid, "unread", threads);
}

void ConversationPreferences::Flush()
{
  if (preferenceStore)
    preferenceStore->Flush();
}

void ConversationPreferences::RemoveThread(int uid, int threadId)
{
  auto state = Load(uid);
  state.pinned.remove(threadId);
  state.unread.remove(threadId);
  state.muted.remove(threadId);
  state.hidden.remove(threadId);
  SaveMuted(uid, state.muted);
  SaveHidden(uid, state.hidden);
  saveThreads(uid, "pinned", state.pinned);
  SaveUnread(uid, state.unread);
  Flush();
}

bool ConversationPreferences::SaveMuted(int uid, const QSet<int> &threads)
{
  auto ids = threads.values();
  std::sort(ids.begin(), ids.end());
  QStringList values;
  for (int id : ids)
    if (id > 0)
      values.append(QString::number(id));
  return saveScoped(uid, "muted", values);
}

bool ConversationPreferences::SaveHidden(int uid, const QHash<int, int> &threads)
{
  auto ids = threads.keys();
  std::sort(ids.begin(), ids.end());
  QStringList values;
  for (int id : ids)
    if (id > 0 && threads.value(id) >= 0)
      values.append(QString("%1:%2").arg(id).arg(threads.value(id)));
  return saveScoped(uid, "hidden", values);
}
