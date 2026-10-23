#include "mobileaccount.h"
#include "common/runtimepaths.h"
#include <qtkeychain/keychain.h>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace {
const QString service = QStringLiteral("CppChatMobile");
}
void MobileAccount::initialize(const QString &origin)
{
  QDir().mkpath(RuntimePaths::preferencesDirectory());
  settingsFile_ = RuntimePaths::preferencesDirectory() + "/mobile-account.ini";
  // Isolate test profiles and server environments, including OS credential entries.
  key_ = QCryptographicHash::hash((origin + '\n' + RuntimePaths::dataDirectory()).toUtf8(),
                                  QCryptographicHash::Sha256)
             .toHex();
  QSettings settings(settingsFile_, QSettings::IniFormat);
  email_ = settings.value("email").toString();
  remember_ = settings.value("remember", false).toBool();
  automatic_ = remember_ && settings.value("automatic", false).toBool();
  if (!remember_) {
    ready_ = true;
    emit changed();
    return;
  }
  const auto revision = ++revision_;
  auto job = new QKeychain::ReadPasswordJob(service, this);
  job->setKey(key_);
  job->setInsecureFallback(false);
  connect(job, &QKeychain::Job::finished, this, [this, job, revision] {
    if (revision != revision_)
      return;
    if (!job->error()) {
      const auto object = QJsonDocument::fromJson(job->binaryData()).object();
      if (object["email"].toString() == email_)
        password_ = object["password"].toString();
    }
    if (password_.isEmpty()) {
      automatic_ = false;
      savePreferences();
      if (job->error() != QKeychain::EntryNotFound)
        emit failed(tr("安全凭据读取失败，请重新输入密码"));
    }
    ready_ = true;
    emit changed();
  });
  job->start();
}
void MobileAccount::savePreferences()
{
  QSettings settings(settingsFile_, QSettings::IniFormat);
  settings.setValue("email", email_);
  settings.setValue("remember", remember_);
  settings.setValue("automatic", automatic_);
  settings.sync();
  if (settings.status() != QSettings::NoError)
    emit failed(tr("账号设置保存失败"));
}
void MobileAccount::setRemember(bool value)
{
  if (remember_ == value)
    return;
  remember_ = value;
  if (!value) {
    automatic_ = false;
    password_.clear();
    eraseSecret();
  }
  if (value && !sessionPassword_.isEmpty()) {
    authenticated(email_, sessionPassword_);
    return;
  }
  savePreferences();
  emit changed();
}
void MobileAccount::setAutomatic(bool value)
{
  automatic_ = value;
  if (value && !remember_)
    setRemember(true);
  savePreferences();
  emit changed();
}
void MobileAccount::authenticated(const QString &email, const QString &password)
{
  email_ = email;
  sessionPassword_ = password;
  password_ = remember_ ? password : QString();
  savePreferences();
  emit changed();
  if (!remember_) {
    eraseSecret();
    return;
  }
  const auto revision = ++revision_;
  auto job = new QKeychain::WritePasswordJob(service, this);
  job->setKey(key_);
  job->setInsecureFallback(false);
  job->setBinaryData(QJsonDocument(QJsonObject{{"email", email}, {"password", password}})
                         .toJson(QJsonDocument::Compact));
  connect(job, &QKeychain::Job::finished, this, [this, job, revision] {
    if (revision != revision_)
      return;
    if (job->error()) {
      automatic_ = false;
      password_.clear();
      savePreferences();
      emit changed();
      emit failed(tr("安全凭据保存失败；下次请手动登录"));
    }
  });
  job->start();
}
void MobileAccount::eraseSecret()
{
  ++revision_;
  ready_ = true;
  auto job = new QKeychain::DeletePasswordJob(service, this);
  job->setKey(key_);
  job->setInsecureFallback(false);
  connect(job, &QKeychain::Job::finished, this, [this, job] {
    if (job->error() && job->error() != QKeychain::EntryNotFound)
      emit failed(tr("安全凭据清除失败，请在系统设置中清除应用数据"));
    else
      emit secretRemoved();
  });
  job->start();
}
void MobileAccount::signedOut()
{
  endSession();
  setAutomatic(false);
}
void MobileAccount::endSession()
{
  sessionPassword_.fill(QChar(0));
  sessionPassword_.clear();
}
void MobileAccount::forget()
{
  email_.clear();
  endSession();
  password_.clear();
  remember_ = automatic_ = false;
  ready_ = true;
  eraseSecret();
  savePreferences();
  emit changed();
}
