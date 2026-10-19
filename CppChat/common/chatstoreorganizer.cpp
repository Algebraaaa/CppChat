#include "chatstore.h"
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <stdexcept>

namespace {
void require(bool valid, const QString &error)
{
  if (!valid)
    throw std::runtime_error(error.toStdString());
}
void run(QSqlQuery &query)
{
  const bool success = query.exec();
  require(success, query.lastError().text());
}
const QString noteColumns = "id,revision,title,body,created_ms,updated_ms";
const QString reminderColumns = "id,thread_id,server_id,subject,excerpt,due_ms,created_ms,due";
LocalNote readNote(const QSqlQuery &query)
{
  return {query.value(0).toLongLong(), query.value(1).toInt(),      query.value(2).toString(),
          query.value(3).toString(),   query.value(4).toLongLong(), query.value(5).toLongLong()};
}
LocalReminder readReminder(const QSqlQuery &query)
{
  return {query.value(0).toLongLong(), query.value(1).toInt(),    query.value(2).toInt(),
          query.value(3).toString(),   query.value(4).toString(), query.value(5).toLongLong(),
          query.value(6).toLongLong(), query.value(7).toBool()};
}
} // namespace

void ChatStore::SaveNote(const LocalNote &note, Callback callback)
{
  Execute(
      [note](QSqlDatabase &database) {
        require(note.id >= 0 && !note.title.trimmed().isEmpty() && note.title.size() <= 80 &&
                    note.text.toUtf8().size() <= 32768,
                "A note needs a title (up to 80 characters) and at most 32 KiB of text");
        const auto now = QDateTime::currentMSecsSinceEpoch();
        QSqlQuery query(database);
        if (note.id == 0) {
          query.prepare("INSERT INTO local_notes(title,body,created_ms,updated_ms) SELECT ?,?,?,? "
                        "WHERE (SELECT COUNT(*) FROM local_notes)<1000 RETURNING " +
                        noteColumns);
          query.addBindValue(note.title.trimmed());
          query.addBindValue(note.text.isNull() ? QString("") : note.text);
          query.addBindValue(now);
          query.addBindValue(now);
        } else {
          require(note.revision > 0, "Invalid note revision");
          query.prepare("UPDATE local_notes SET title=?,body=?,revision=revision+1,updated_ms=? "
                        "WHERE id=? AND revision=? RETURNING " +
                        noteColumns);
          query.addBindValue(note.title.trimmed());
          query.addBindValue(note.text.isNull() ? QString("") : note.text);
          query.addBindValue(now);
          query.addBindValue(note.id);
          query.addBindValue(note.revision);
        }
        run(query);
        require(query.next(), "Note changed, was deleted, or the 1000-note limit was reached; "
                              "reload before retrying");
        ChatStoreResult result;
        result.notes.append(readNote(query));
        require(!query.next(), "Unexpected note result");
        require(!query.lastError().isValid(), query.lastError().text());
        return result;
      },
      [this, callback](const ChatStoreResult &result) {
        if (result.error.isEmpty())
          emit notesChanged();
        if (callback)
          callback(result);
      });
}

void ChatStore::LoadNotes(const QString &text, qint64 beforeId, Callback callback)
{
  Execute(
      [text, beforeId](QSqlDatabase &database) {
        auto pattern = text.trimmed().left(256);
        pattern.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_");
        pattern = '%' + pattern + '%';
        QSqlQuery query(database);
        query.prepare("SELECT id,revision,title,'',created_ms,updated_ms FROM local_notes WHERE "
                      "(?=0 OR id<?) AND (title LIKE ? ESCAPE '\\' OR body LIKE ? ESCAPE '\\') "
                      "ORDER BY id DESC LIMIT 51");
        query.addBindValue(beforeId);
        query.addBindValue(beforeId);
        query.addBindValue(pattern);
        query.addBindValue(pattern);
        run(query);
        ChatStoreResult result;
        while (query.next()) {
          if (result.notes.size() == 50) {
            result.more = true;
            break;
          }
          result.notes.append(readNote(query));
        }
        return result;
      },
      callback);
}

void ChatStore::GetNote(qint64 id, Callback callback)
{
  Execute(
      [id](QSqlDatabase &database) {
        require(id > 0, "Invalid note");
        QSqlQuery query(database);
        query.prepare("SELECT " + noteColumns + " FROM local_notes WHERE id=?");
        query.addBindValue(id);
        run(query);
        require(query.next(), "Note no longer exists");
        ChatStoreResult result;
        result.notes.append(readNote(query));
        return result;
      },
      callback);
}

void ChatStore::DeleteNote(qint64 id, int revision, Callback callback)
{
  Execute(
      [id, revision](QSqlDatabase &database) {
        require(id > 0 && revision > 0, "Invalid note");
        QSqlQuery query(database);
        query.prepare("DELETE FROM local_notes WHERE id=? AND revision=?");
        query.addBindValue(id);
        query.addBindValue(revision);
        run(query);
        require(query.numRowsAffected() == 1, "Note changed or was already deleted; reload first");
        return ChatStoreResult{};
      },
      [this, callback](const ChatStoreResult &result) {
        if (result.error.isEmpty())
          emit notesChanged();
        if (callback)
          callback(result);
      });
}

void ChatStore::SaveReminder(const LocalReminder &reminder, Callback callback)
{
  Execute(
      [reminder](QSqlDatabase &database) {
        const auto now = QDateTime::currentMSecsSinceEpoch();
        require(reminder.id >= 0 && reminder.dueMs > now &&
                    reminder.dueMs <= now + qint64(366) * 86400000,
                "Choose a future reminder time within one year");
        QSqlQuery query(database);
        if (reminder.id == 0) {
          require(reminder.threadId > 0 && reminder.serverId > 0 &&
                      !reminder.subject.trimmed().isEmpty() && reminder.subject.size() <= 128 &&
                      reminder.excerpt.size() <= 512,
                  "Invalid message reminder");
          query.prepare(
              "INSERT INTO local_reminders(thread_id,server_id,subject,excerpt,due_ms,created_ms) "
              "SELECT ?,?,?,?,?,? WHERE (SELECT COUNT(*) FROM local_reminders)<500 AND "
              "EXISTS(SELECT 1 FROM messages WHERE thread_id=? AND server_id=?) RETURNING " +
              reminderColumns);
          query.addBindValue(reminder.threadId);
          query.addBindValue(reminder.serverId);
          query.addBindValue(reminder.subject);
          query.addBindValue(reminder.excerpt.isNull() ? QString("") : reminder.excerpt);
          query.addBindValue(reminder.dueMs);
          query.addBindValue(now);
          query.addBindValue(reminder.threadId);
          query.addBindValue(reminder.serverId);
        } else {
          query.prepare("UPDATE local_reminders SET due_ms=?,due=0 WHERE id=? RETURNING " +
                        reminderColumns);
          query.addBindValue(reminder.dueMs);
          query.addBindValue(reminder.id);
        }
        run(query);
        require(query.next(), "Reminder missing, message deleted, or 500-reminder limit reached");
        ChatStoreResult result;
        result.reminders.append(readReminder(query));
        require(!query.next(), "Unexpected reminder result");
        require(!query.lastError().isValid(), query.lastError().text());
        return result;
      },
      [this, callback](const ChatStoreResult &result) {
        if (result.error.isEmpty())
          emit remindersChanged();
        if (callback)
          callback(result);
      });
}

void ChatStore::LoadReminders(Callback callback)
{
  Execute(
      [](QSqlDatabase &database) {
        QSqlQuery query(database);
        query.prepare("SELECT " + reminderColumns +
                      " FROM local_reminders ORDER BY due_ms,id LIMIT 500");
        run(query);
        ChatStoreResult result;
        while (query.next())
          result.reminders.append(readReminder(query));
        require(!query.lastError().isValid(), query.lastError().text());
        return result;
      },
      callback);
}

void ChatStore::CheckDueReminders(qint64 now, Callback callback)
{
  Execute(
      [now](QSqlDatabase &database) {
        require(now > 0, "Invalid reminder clock");
        QSqlQuery query(database);
        // Commit only new transitions. Reading the durable due set also recovers
        // unacknowledged items after restart, without rewriting them every tick.
        query.prepare("UPDATE local_reminders SET due=1 WHERE due=0 AND due_ms<=?");
        query.addBindValue(now);
        run(query);
        query.prepare("SELECT " + reminderColumns +
                      " FROM local_reminders WHERE due=1 ORDER BY due_ms,id");
        run(query);
        ChatStoreResult result;
        while (query.next())
          result.reminders.append(readReminder(query));
        require(!query.lastError().isValid(), query.lastError().text());
        return result;
      },
      callback);
}

void ChatStore::RemoveReminder(qint64 id, Callback callback)
{
  Execute(
      [id](QSqlDatabase &database) {
        require(id > 0, "Invalid reminder");
        QSqlQuery query(database);
        query.prepare("DELETE FROM local_reminders WHERE id=?");
        query.addBindValue(id);
        run(query);
        return ChatStoreResult{};
      },
      [this, callback](const ChatStoreResult &result) {
        if (result.error.isEmpty())
          emit remindersChanged();
        if (callback)
          callback(result);
      });
}
