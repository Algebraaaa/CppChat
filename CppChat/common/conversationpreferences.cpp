#include "conversationpreferences.h"
#include "runtimepaths.h"
#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QLoggingCategory>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
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
  return {readThreads(preferences->Read(accountKey(uid, "pinned"))),
          readThreads(preferences->Read(accountKey(uid, "unread")))};
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
  saveThreads(uid, "pinned", state.pinned);
  SaveUnread(uid, state.unread);
  Flush();
}
