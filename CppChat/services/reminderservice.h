#pragma once
#include "common/localorganizer.h"
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVector>

// Polling is account-bound. Durable due state remains until the user acknowledges it.
class ReminderService : public QObject {
  Q_OBJECT
public:
  explicit ReminderService(QObject *parent = nullptr);
  void CheckNow(qint64 now = 0);
signals:
  void dueAvailable(QVector<LocalReminder> reminders);

private:
  void Reset();
  QTimer _timer;
  QHash<qint64, qint64> _notified;
  bool _busy = false, _again = false;
  quint64 _revision = 0;
  qint64 _nextNow = 0;
};
