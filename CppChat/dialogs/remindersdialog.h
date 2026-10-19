#pragma once
#include "common/localorganizer.h"
#include <QDialog>
#include <QMap>
#include <QPointer>
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class RemindersDialog : public QDialog {
  Q_OBJECT
public:
  explicit RemindersDialog(QWidget *parent = nullptr);
  void Schedule(const LocalReminder &reminder);
  void Reload();
signals:
  void openThreadRequested(int threadId);

private:
  void Controls();
  void Filter();
  void Remove();
  QLineEdit *_search;
  QListWidget *_list;
  QLabel *_status;
  QPushButton *_change, *_remove, *_open;
  QPointer<QDialog> _editor;
  QMap<qint64, LocalReminder> _values;
  quint64 _revision = 0;
  bool _busy = false;
};
