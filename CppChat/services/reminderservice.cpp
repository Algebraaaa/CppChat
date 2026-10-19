#include "reminderservice.h"
#include "common/chatstore.h"
#include <QDateTime>
#include <QPointer>
#include <QSet>

ReminderService::ReminderService(QObject *parent) : QObject(parent)
{
  _timer.setInterval(15000);
  connect(&_timer, &QTimer::timeout, this, [this] { CheckNow(); });
  connect(ChatStore::instance(), &ChatStore::accountChanged, this, &ReminderService::Reset);
  connect(ChatStore::instance(), &ChatStore::accountReady, this, [this] {
    _timer.start();
    CheckNow();
  });
  connect(ChatStore::instance(), &ChatStore::remindersChanged, this, [this] { CheckNow(); });
  if (ChatStore::instance()->IsReady()) {
    _timer.start();
    CheckNow();
  }
}
void ReminderService::Reset()
{
  ++_revision;
  _timer.stop();
  _busy = _again = false;
  _notified.clear();
  _nextNow = 0;
}
void ReminderService::CheckNow(qint64 now)
{
  if (!ChatStore::instance()->IsReady())
    return;
  if (_busy) {
    _again = true;
    _nextNow = qMax(_nextNow, now);
    return;
  }
  _busy = true;
  const auto revision = _revision;
  const QPointer<ReminderService> guard(this);
  ChatStore::instance()->CheckDueReminders(
      now > 0 ? now : QDateTime::currentMSecsSinceEpoch(),
      [guard, revision](const ChatStoreResult &result) {
        if (!guard || guard->_revision != revision)
          return;
        guard->_busy = false;
        if (result.error.isEmpty()) {
          QSet<qint64> active;
          QVector<LocalReminder> fresh;
          for (const auto &reminder : result.reminders) {
            active.insert(reminder.id);
            if (guard->_notified.value(reminder.id) != reminder.dueMs) {
              guard->_notified.insert(reminder.id, reminder.dueMs);
              fresh.append(reminder);
            }
          }
          for (auto it = guard->_notified.begin(); it != guard->_notified.end();)
            if (!active.contains(it.key()))
              it = guard->_notified.erase(it);
            else
              ++it;
          if (!fresh.isEmpty())
            emit guard->dueAvailable(fresh);
        }
        if (guard && guard->_again) {
          guard->_again = false;
          const auto next = guard->_nextNow;
          guard->_nextNow = 0;
          QTimer::singleShot(0, guard, [guard, next] {
            if (guard)
              guard->CheckNow(next);
          });
        }
      });
}
