#pragma once
#include <QDialog>
#include <QJsonObject>
#include <QList>

class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;
class QTabWidget;

// Creation and membership editing share one correlated request at a time.
class GroupDialog : public QDialog {
  Q_OBJECT
public:
  explicit GroupDialog(int threadId = 0, QWidget *parent = nullptr);
  int ThreadId() const { return _threadId; }
signals:
  void groupOpened(int threadId);

private:
  void BuildUi();
  void BindActions();
  void BindNetwork();
  void Send(const QString &operation, QJsonObject fields = {});
  void Refresh(const QJsonObject &group);
  void SetBusy(bool busy);
  void UpdateActions();
  void UpdateSelection();
  void FilterLists();
  QList<int> CheckedFriends() const;
  QString CreationName() const;
  void InviteNext();
  int SelectedMember() const;
  int _threadId = 0;
  int _owner = 0;
  bool _busy = false;
  QString _creationKey;
  QString _pendingOperation;
  QString _serverName;
  QList<int> _inviteQueue;
  int _inviteCompleted = 0;
  int _memberCount = 0;
  QLineEdit *_name;
  QLineEdit *_search;
  QTabWidget *_tabs;
  QListWidget *_members;
  QListWidget *_friends;
  QListWidget *_selected;
  QLabel *_summary;
  QLabel *_selectionCount;
  QLabel *_empty;
  QLabel *_status;
  QPushButton *_save;
  QPushButton *_add;
  QPushButton *_remove;
  QPushButton *_transfer;
  QPushButton *_leave;
  QPushButton *_close;
};
