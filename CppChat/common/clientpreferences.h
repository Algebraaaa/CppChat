#pragma once
#include <QObject>

// Non-secret, device-local settings. Account credentials remain in AutoLoginStore.
class ClientPreferences : public QObject {
  Q_OBJECT
public:
  enum class SendKey { Enter, ControlEnter };
  Q_ENUM(SendKey)
  enum class Appearance { System, Light, Dark };
  Q_ENUM(Appearance)
  struct State {
    SendKey sendKey = SendKey::Enter;
    bool checkUpdatesOnStartup = true;
    Appearance appearance = Appearance::System;
  };
  static ClientPreferences *instance();
  explicit ClientPreferences(const QString &fileName, QObject *parent = nullptr);
  State Current() const { return _state; }
  QString LoadError() const { return _loadError; }
  bool Save(const State &state, QString &error);
signals:
  void changed();

private:
  QString _fileName;
  QString _loadError;
  bool _futureVersion = false;
  State _state;
};
