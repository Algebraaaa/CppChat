#pragma once
#include <QMetaType>
#include <QString>

// These records belong to the currently opened encrypted account database.
struct LocalNote {
  qint64 id = 0;
  int revision = 0;
  QString title;
  QString text;
  qint64 createdMs = 0;
  qint64 updatedMs = 0;
};

struct LocalReminder {
  qint64 id = 0;
  int threadId = 0;
  int serverId = 0;
  QString subject;
  QString excerpt;
  qint64 dueMs = 0;
  qint64 createdMs = 0;
  bool due = false;
};
Q_DECLARE_METATYPE(LocalReminder)
