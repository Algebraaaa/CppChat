#pragma once
#include <QObject>
#include <QString>

// Account preferences contain no secret; QtKeychain owns OS-backed credential storage.
class MobileAccount : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QString email READ email NOTIFY changed)
  Q_PROPERTY(QString password READ password NOTIFY changed)
  Q_PROPERTY(bool remember READ remember WRITE setRemember NOTIFY changed)
  Q_PROPERTY(bool automatic READ automatic WRITE setAutomatic NOTIFY changed)
  Q_PROPERTY(bool ready READ ready NOTIFY changed)
public:
  explicit MobileAccount(QObject *parent = nullptr) : QObject(parent) {}
  void initialize(const QString &origin);
  QString email() const
  {
    return email_;
  }
  QString password() const
  {
    return password_;
  }
  bool remember() const
  {
    return remember_;
  }
  bool automatic() const
  {
    return automatic_;
  }
  bool ready() const
  {
    return ready_;
  }
  void setRemember(bool value);
  void setAutomatic(bool value);
  void authenticated(const QString &email, const QString &password);
  void signedOut();
  void endSession();
  Q_INVOKABLE void forget();
signals:
  void changed();
  void failed(const QString &message);
  void secretRemoved();

private:
  void savePreferences();
  void eraseSecret();
  QString email_, password_, key_, settingsFile_;
  QString sessionPassword_;
  bool remember_ = false, automatic_ = false, ready_ = false;
  quint64 revision_ = 0;
};
