#pragma once
#include <QDialog>
#include <QPointer>
class QLineEdit;
class QComboBox;
class QCheckBox;
class QTableView;
class QStandardItemModel;
class QSortFilterProxyModel;
class QLabel;
class QPushButton;
class ApplyFriend;

class ContactManagerDialog : public QDialog {
  Q_OBJECT
public:
  explicit ContactManagerDialog(QWidget *parent = nullptr);
  void Refresh();
signals:
  void openProfileRequested(int uid);

private:
  int SelectedUid() const;
  void Controls();
  int _uid;
  QLineEdit *_search;
  QComboBox *_tag;
  QCheckBox *_starred;
  QTableView *_view;
  QStandardItemModel *_model;
  QSortFilterProxyModel *_proxy;
  QLabel *_status;
  QPushButton *_profile, *_edit, *_star;
  QPointer<ApplyFriend> _metadata;
};
