#pragma once
#include <QString>

// Set once, before constructing settings, stores or network managers.
namespace RuntimePaths {
void configure(const QString &config, const QString &data, const QString &logs);
QString configFile();
QString dataDirectory();
QString logDirectory();
QString preferencesDirectory();
} // namespace RuntimePaths
