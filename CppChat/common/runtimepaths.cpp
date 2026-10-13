#include "runtimepaths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
QString configPath, dataPath, logsPath;
QString absolute(const QString &path)
{
  return path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
}
} // namespace
namespace RuntimePaths {
void configure(const QString &config, const QString &data, const QString &logs)
{
  configPath = absolute(config);
  dataPath = absolute(data);
  logsPath = absolute(logs);
}
QString configFile()
{
  return configPath.isEmpty() ? QCoreApplication::applicationDirPath() + "/config.ini" : configPath;
}
QString dataDirectory() { return dataPath; }
QString logDirectory() { return logsPath; }
QString preferencesDirectory()
{
  return dataPath.isEmpty()
             ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/CppChat"
             : QDir(dataPath).filePath("preferences");
}
} // namespace RuntimePaths
