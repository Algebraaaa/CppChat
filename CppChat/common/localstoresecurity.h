#pragma once
#include <QByteArray>
#include <QSqlDatabase>
#include <QString>

namespace LocalStoreSecurity {
QString driverName();
QByteArray prepareAccount(const QString &directory);
QByteArray accountKey(const QString &directory, bool create = false);
void configureDatabase(QSqlDatabase &database, const QByteArray &key);
void restoreBackup(const QString &backup, const QString &emptyDestination);
int prepareRoot(const QString &root);
void replaceAccountKey(const QString &directory, const QByteArray &key);
} // namespace LocalStoreSecurity
