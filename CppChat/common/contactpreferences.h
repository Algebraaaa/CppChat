#pragma once
#include <QSet>
#include <QString>

// Explicit local preferences, isolated by server and account. Never stores contact content.
namespace ContactPreferences {
QSet<int> LoadStarred(int account, const QString &server);
bool SaveStarred(int account, const QString &server, const QSet<int> &contacts);
} // namespace ContactPreferences
