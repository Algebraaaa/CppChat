#pragma once
#include <QByteArray>
#include <QString>

namespace KeyRecovery {
QByteArray exportKeys(const QString &dataRoot, const QString &password);
int importKeys(const QString &dataRoot, const QByteArray &recovery, const QString &password);
} // namespace KeyRecovery
