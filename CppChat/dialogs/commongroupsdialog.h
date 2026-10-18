#pragma once
#include <QDialog>
#include <QJsonObject>
#include <QMap>

class QLabel;
class QListWidget;
class QPushButton;

// Reads server-authorized shared groups; never infers membership from cached message names.
class CommonGroupsDialog : public QDialog {
  Q_OBJECT
public:
  explicit CommonGroupsDialog(int friendUid, QWidget *parent = nullptr);
signals:
  void groupOpened(int threadId);

private:
  void Load(bool reset);
  void Receive(const QJsonObject &response);
  void OpenSelected();
  void UpdateControls();
  int _friendUid, _cursor = 0;
  bool _busy = false, _hasMore = true;
  QMap<int, QJsonObject> _groups;
  QListWidget *_list;
  QLabel *_status;
  QPushButton *_refresh, *_more, *_open;
};
