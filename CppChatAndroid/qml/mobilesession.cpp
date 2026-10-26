#include "mobilesession.h"
#include <QRandomGenerator>
#include <algorithm>

MobileSession::MobileSession(QObject *parent) : QObject(parent)
{
  timer_.setSingleShot(true);
  connect(&timer_, &QTimer::timeout, this, [this] {
    if (!recovering_ || attempting_ || !available_ || password_.isEmpty())
      return;
    attempting_ = true;
    emit changed();
    emit reconnectRequested();
  });
}
MobileSession::~MobileSession()
{
  stop();
}
void MobileSession::prepare(const QString &email, const QString &password)
{
  stop();
  email_ = email;
  password_ = password;
}
void MobileSession::authenticated()
{
  timer_.stop();
  active_ = true;
  recovering_ = attempting_ = false;
  failures_ = 0;
  emit changed();
}
void MobileSession::interrupted()
{
  if (password_.isEmpty() || recovering_)
    return;
  recovering_ = true;
  attempting_ = false;
  schedule();
  emit changed();
}
void MobileSession::retryFailed()
{
  if (!recovering_)
    return;
  attempting_ = false;
  schedule();
  emit changed();
}
void MobileSession::schedule()
{
  if (!available_)
    return;
  // Retry indefinitely with a bounded delay; jitter avoids a reconnect storm
  // after a server restart. Connectivity/resume notifications bypass the delay.
  const int delay = std::min(30000, 1000 * (1 << std::min(failures_, 5)));
  failures_ = std::min(failures_ + 1, 6);
  timer_.start(delay + QRandomGenerator::global()->bounded(251));
}
void MobileSession::stop()
{
  timer_.stop();
  email_.clear();
  password_.fill(QChar(0));
  password_.clear();
  active_ = recovering_ = attempting_ = false;
  failures_ = 0;
  emit changed();
}
void MobileSession::setAvailable(bool available)
{
  if (available_ == available)
    return;
  available_ = available;
  if (!available)
    timer_.stop();
  else
    retryNow();
}
void MobileSession::retryNow()
{
  if (recovering_ && available_ && !attempting_ && !password_.isEmpty())
    timer_.start(0);
}
