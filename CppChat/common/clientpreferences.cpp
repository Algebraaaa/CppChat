#include "clientpreferences.h"
#include "runtimepaths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

ClientPreferences *ClientPreferences::instance()
{
  static auto *store = new ClientPreferences(RuntimePaths::preferencesDirectory() + "/client.json",
                                             QCoreApplication::instance());
  return store;
}

ClientPreferences::ClientPreferences(const QString &fileName, QObject *parent)
    : QObject(parent), _fileName(fileName)
{
  QFile file(fileName);
  if (!file.exists())
    return;
  if (!file.open(QIODevice::ReadOnly) || file.size() > 4096) {
    _loadError = tr("无法读取本机设置，当前使用默认值");
    return;
  }
  const auto document = QJsonDocument::fromJson(file.readAll());
  const auto value = document.object();
  _futureVersion = value["schema"].toInt() > 1;
  if (!document.isObject() || value["schema"] != QJsonValue(1) ||
      !value["checkUpdatesOnStartup"].isBool() ||
      (value["sendKey"].toString() != "enter" && value["sendKey"].toString() != "control-enter")) {
    _loadError = tr("本机设置格式不受支持，当前使用默认值");
    return;
  }
  _state.sendKey = value["sendKey"].toString() == "enter" ? SendKey::Enter : SendKey::ControlEnter;
  _state.checkUpdatesOnStartup = value["checkUpdatesOnStartup"].toBool();
  const auto appearance = value.value("appearance").toString("system");
  if (appearance == "system" || appearance == "light" || appearance == "dark")
    _state.appearance = appearance == "light"  ? Appearance::Light
                        : appearance == "dark" ? Appearance::Dark
                                               : Appearance::System;
  else
    _loadError = tr("外观设置无效，当前跟随系统");
}

bool ClientPreferences::Save(const State &state, QString &error)
{
  error.clear();
  if (_futureVersion ||
      (state.sendKey != SendKey::Enter && state.sendKey != SendKey::ControlEnter) ||
      (state.appearance != Appearance::System && state.appearance != Appearance::Light &&
       state.appearance != Appearance::Dark)) {
    error = tr("设置版本或发送快捷键不受支持，原设置保留");
    return false;
  }
  if (!QDir().mkpath(QFileInfo(_fileName).absolutePath())) {
    error = tr("无法创建设置目录，原设置保留");
    return false;
  }
  const auto bytes =
      QJsonDocument(
          QJsonObject{{"schema", 1},
                      {"sendKey", state.sendKey == SendKey::Enter ? "enter" : "control-enter"},
                      {"checkUpdatesOnStartup", state.checkUpdatesOnStartup},
                      {"appearance", state.appearance == Appearance::System ? "system"
                                     : state.appearance == Appearance::Dark ? "dark"
                                                                            : "light"}})
          .toJson();
  QSaveFile file(_fileName);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
    error = tr("设置保存失败，原设置保留：%1").arg(file.errorString());
    return false;
  }
  _state = state;
  _loadError.clear();
  emit changed();
  return true;
}
