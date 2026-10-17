#pragma once
#include <QDialog>
#include <QJsonObject>

class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;

// Creation and membership editing share one correlated request at a time.
class GroupDialog : public QDialog {
  Q_OBJECT
public:
  explicit GroupDialog(int threadId = 0, QWidget *parent = nullptr);
signals:
  void groupOpened(int threadId);

private:
  void Send(const QString &operation, QJsonObject fields = {});
  void Refresh(const QJsonObject &group);
  void SetBusy(bool busy);
  int SelectedMember() const;
  int _threadId = 0;
  int _owner = 0;
  bool _busy = false;
  QString _creationKey;
  QLineEdit *_name;
  QListWidget *_members;
  QListWidget *_friends;
  QLabel *_status;
  QPushButton *_save;
  QPushButton *_add;
  QPushButton *_remove;
  QPushButton *_transfer;
  QPushButton *_leave;
};
