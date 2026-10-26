#pragma once
#include <QObject>
#include <QTimer>

// Connection recovery policy only. The facade performs authentication and the
// shared desktop services remain responsible for transport and durable history.
class MobileSession : public QObject
{
  Q_OBJECT
public:
  explicit MobileSession(QObject *parent = nullptr);
  ~MobileSession() override;
  bool active() const
  {
    return active_;
  }
  bool recovering() const
  {
    return recovering_;
  }
  bool attempting() const
  {
    return attempting_;
  }
  QString email() const
  {
    return email_;
  }
  QString password() const
  {
    return password_;
  }
  void prepare(const QString &email, const QString &password);
  void authenticated();
  void interrupted();
  void retryFailed();
  void stop();
  void setAvailable(bool available);
  void retryNow();
signals:
  void changed();
  void reconnectRequested();

private:
  void schedule();
  QTimer timer_;
  QString email_, password_;
  bool active_ = false, recovering_ = false, attempting_ = false, available_ = true;
  int failures_ = 0;
};
