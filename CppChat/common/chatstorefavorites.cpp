#include "chatstore.h"
#include "media/pcmwave.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <stdexcept>

namespace {
constexpr qsizetype kMediaLimit = 20 * 1024 * 1024;
constexpr qsizetype kBatchLimit = 64 * 1024 * 1024;
void require(bool valid, const QString &error)
{
  if (!valid)
    throw std::runtime_error(
        (error.isEmpty() ? QStringLiteral("Favorite storage operation failed") : error)
            .toStdString());
}
void execute(QSqlQuery &query)
{
  const bool success = query.exec();
  require(success, query.lastError().text());
}

class FavoriteTransaction {
public:
  explicit FavoriteTransaction(QSqlDatabase &database) : database_(database)
  {
    const bool success = database_.transaction();
    require(success, database_.lastError().text());
  }
  ~FavoriteTransaction()
  {
    if (!committed_)
      database_.rollback();
  }
  void commit()
  {
    const bool success = database_.commit();
    require(success, database_.lastError().text());
    committed_ = true;
  }

private:
  QSqlDatabase &database_;
  bool committed_ = false;
};

StoredFavorite readFavorite(const QSqlQuery &query)
{
  StoredFavorite value;
  value.id = query.value(0).toLongLong();
  value.threadId = query.value(1).toInt();
  value.serverId = query.value(2).toInt();
  value.senderUid = query.value(3).toInt();
  value.type = query.value(4).toInt();
  value.senderName = query.value(5).toString();
  value.content = query.value(6).toString();
  value.createdMs = query.value(7).toLongLong();
  return value;
}
const QString columns =
    QStringLiteral("id,thread_id,server_id,sender_uid,message_type,sender_name,content,created_ms");
} // namespace

void ChatStore::SaveFavorites(const QVector<StoredFavorite> &favorites, Callback callback)
{
  Execute(
      [favorites](QSqlDatabase &database) {
        require(!favorites.isEmpty() && favorites.size() <= 50, "Select between 1 and 50 messages");
        qsizetype bytes = 0;
        for (const auto &value : favorites) {
          bytes += value.media.size();
          require(value.threadId > 0 && value.serverId > 0 && value.senderUid > 0 &&
                      value.senderName.size() <= 256 && value.content.toUtf8().size() <= 65536 &&
                      ((value.type == 0 && value.media.isEmpty()) ||
                       (value.type == 1 && !value.media.isEmpty() &&
                        value.media.size() <= kMediaLimit) ||
                       (value.type == 3 &&
                        cppchat::media::WaveDurationMs(
                            std::string_view(value.media.constData(), value.media.size()),
                            value.media.size()) > 0)),
                  "Invalid favorite snapshot or missing media");
        }
        require(bytes <= kBatchLimit, "Favorite media exceeds the 64 MiB batch limit");
        FavoriteTransaction transaction(database);
        for (const auto &value : favorites) {
          QSqlQuery query(database);
          query.prepare(
              "INSERT INTO favorites(thread_id,server_id,sender_uid,message_type,sender_name,"
              "content,media,media_sha256,created_ms) VALUES(?,?,?,?,?,?,?,?,?) "
              "ON CONFLICT(thread_id,server_id) DO NOTHING");
          query.addBindValue(value.threadId);
          query.addBindValue(value.serverId);
          query.addBindValue(value.senderUid);
          query.addBindValue(value.type);
          query.addBindValue(value.senderName);
          query.addBindValue(value.content);
          // A null QByteArray would bind SQL NULL and violate the explicit snapshot contract.
          query.addBindValue(value.media.isEmpty() ? QByteArray("") : value.media);
          query.addBindValue(QString::fromLatin1(
              QCryptographicHash::hash(value.media, QCryptographicHash::Sha256).toHex()));
          query.addBindValue(QDateTime::currentMSecsSinceEpoch());
          execute(query);
        }
        transaction.commit();
        return ChatStoreResult{};
      },
      [this, callback](const ChatStoreResult &result) {
        if (result.error.isEmpty())
          emit favoritesChanged();
        if (callback)
          callback(result);
      });
}

void ChatStore::LoadFavorites(const QString &text, FavoriteCategory category, qint64 beforeId,
                              Callback callback)
{
  Execute(
      [text, category, beforeId](QSqlDatabase &database) {
        QString pattern = text.trimmed().left(256);
        pattern.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_");
        pattern = '%' + pattern + '%';
        QString filter;
        switch (category) {
        case FavoriteCategory::Recent:
          filter = " AND created_ms>=?";
          break;
        case FavoriteCategory::Links:
          filter =
              " AND message_type=0 AND (content LIKE '%https://%' OR content LIKE '%http://%')";
          break;
        case FavoriteCategory::Images:
          filter = " AND message_type=1";
          break;
        case FavoriteCategory::Messages:
          filter = " AND message_type=0";
          break;
        case FavoriteCategory::Audio:
          filter = " AND message_type=3";
          break;
        case FavoriteCategory::All:
          break;
        }
        QSqlQuery query(database);
        query.prepare("SELECT " + columns +
                      " FROM favorites WHERE (?=0 OR id<?) AND "
                      "(content LIKE ? ESCAPE '\\' OR sender_name LIKE ? ESCAPE '\\')" +
                      filter + " ORDER BY id DESC LIMIT 51");
        query.addBindValue(beforeId);
        query.addBindValue(beforeId);
        query.addBindValue(pattern);
        query.addBindValue(pattern);
        if (category == FavoriteCategory::Recent)
          query.addBindValue(QDateTime::currentMSecsSinceEpoch() - qint64(7) * 86400000);
        execute(query);
        ChatStoreResult result;
        while (query.next()) {
          if (result.favorites.size() == 50) {
            result.more = true;
            break;
          }
          result.favorites.append(readFavorite(query));
        }
        return result;
      },
      callback);
}

void ChatStore::GetFavorite(qint64 id, Callback callback)
{
  Execute(
      [id](QSqlDatabase &database) {
        require(id > 0, "Invalid favorite identity");
        QSqlQuery query(database);
        query.prepare("SELECT " + columns +
                      ",media,media_sha256 FROM favorites WHERE id=? "
                      "AND length(media)<=?");
        query.addBindValue(id);
        query.addBindValue(qint64(kMediaLimit));
        execute(query);
        require(query.next(), "Favorite no longer exists or its media is invalid");
        auto value = readFavorite(query);
        value.media = query.value(8).toByteArray();
        require(QString::fromLatin1(
                    QCryptographicHash::hash(value.media, QCryptographicHash::Sha256).toHex()) ==
                    query.value(9).toString(),
                "Favorite media checksum mismatch");
        ChatStoreResult result;
        result.favorites.append(value);
        return result;
      },
      callback);
}

void ChatStore::RemoveFavorites(const QVector<qint64> &ids, Callback callback)
{
  Execute(
      [ids](QSqlDatabase &database) {
        require(!ids.isEmpty() && ids.size() <= 50, "Select between 1 and 50 favorites");
        FavoriteTransaction transaction(database);
        for (auto id : ids) {
          require(id > 0, "Invalid favorite identity");
          QSqlQuery query(database);
          query.prepare("DELETE FROM favorites WHERE id=?");
          query.addBindValue(id);
          execute(query);
        }
        transaction.commit();
        return ChatStoreResult{};
      },
      [this, callback](const ChatStoreResult &result) {
        if (result.error.isEmpty())
          emit favoritesChanged();
        if (callback)
          callback(result);
      });
}
