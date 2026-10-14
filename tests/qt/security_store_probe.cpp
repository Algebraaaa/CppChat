#include "common/localdataprotection.h"
#include "common/localstoresecurity.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <iostream>
#include <stdexcept>

void require(bool ok, const char *error)
{
  if (!ok)
    throw std::runtime_error(error);
}
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  try {
    const auto args = app.arguments();
    require(args.size() == 3, "Usage: security_store_probe create|audit <account-directory>");
    const QString directory = args.at(2);
    const bool creating = args.at(1) == "create";
    const QString path = directory + "/sql/db.sqlite";
    if (creating) {
      require(!QFile::exists(path), "Probe refuses to replace an existing database");
      require(QDir().mkpath(directory + "/sql") && QDir().mkpath(directory + "/attachments"),
              "Cannot create owned fixture");
    }
    QFile header(path);
    const bool plain = creating || (header.open(QIODevice::ReadOnly) &&
                                    header.read(16) == QByteArray("SQLite format 3\0", 16));
    header.close();
    auto database =
        QSqlDatabase::addDatabase(plain ? QString("QSQLITE") : LocalStoreSecurity::driverName());
    database.setDatabaseName(path);
    if (!plain)
      LocalStoreSecurity::configureDatabase(database, LocalStoreSecurity::accountKey(directory));
    if (!creating)
      database.setConnectOptions(database.connectOptions() + ";QSQLITE_OPEN_READONLY");
    require(database.open(), "Cannot open audited database");
    if (creating) {
      QFile schema(":/storage/chatstore-schema.sql");
      require(schema.open(QIODevice::ReadOnly), "Schema unavailable");
      QSqlQuery query(database);
      for (const auto &statement : QString::fromUtf8(schema.readAll()).split(';'))
        if (!statement.trimmed().isEmpty())
          require(query.exec(statement), "Cannot create fixture schema");
      require(query.exec("INSERT INTO conversations(thread_id,other_uid) VALUES(7,102)"),
              "Cannot seed fixture");
      require(
          query.exec("INSERT INTO "
                     "messages(thread_id,sender_uid,server_id,unique_id,content,message_type,chat_"
                     "time) VALUES(7,101,1,'package-owned','package-chat-secret',0,'2026-10-13')"),
          "Cannot seed message");
      require(query.exec("INSERT INTO drafts(thread_id,ordinal,message_flag,content) "
                         "VALUES(7,0,'text','package-draft-secret')"),
              "Cannot seed draft");
      LocalDataProtection::atomicWrite(directory + "/attachments/package-image",
                                       "package-image-secret");
    }
    QSqlQuery tables(database);
    require(tables.exec("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE "
                        "'sqlite_%' ORDER BY name"),
            "Cannot enumerate tables");
    QJsonObject result;
    while (tables.next()) {
      const QString name = tables.value(0).toString();
      QString quoted = name;
      quoted.replace('"', "\"\"");
      QSqlQuery rows(database);
      require(rows.exec("SELECT * FROM \"" + quoted + "\" LIMIT 0"), "Cannot inspect table");
      QStringList order;
      for (int i = 0; i < rows.record().count(); ++i)
        order.append(QString::number(i + 1));
      require(rows.exec("SELECT * FROM \"" + quoted + "\" ORDER BY " + order.join(',')),
              "Cannot audit table");
      QCryptographicHash hash(QCryptographicHash::Sha256);
      int count = 0;
      while (rows.next()) {
        ++count;
        for (int i = 0; i < rows.record().count(); ++i) {
          const auto value = rows.value(i);
          const QByteArray bytes = value.isNull() ? QByteArray("NULL")
                                   : value.metaType().id() == QMetaType::QByteArray
                                       ? value.toByteArray()
                                       : value.toString().toUtf8();
          hash.addData(QByteArray::number(bytes.size()) + ':' + bytes);
        }
      }
      result.insert(name, QJsonObject{{"rows", count},
                                      {"sha256", QString::fromLatin1(hash.result().toHex())}});
    }
    std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << std::endl;
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
