#pragma once
#include "common/localorganizer.h"
#include <QDialog>
#include <QTimer>

class QLineEdit;
class QPlainTextEdit;
class QListWidget;
class QLabel;
class QPushButton;
class QCloseEvent;

class NotesDialog : public QDialog {
  Q_OBJECT
public:
  explicit NotesDialog(QWidget *parent = nullptr);
  void reject() override;
signals:
  void sendRequested(QString text);

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  bool CanDiscard();
  void NewNote();
  void Open();
  void Save();
  void Remove();
  void Load(bool append = false);
  void SetNote(const LocalNote &note);
  void Controls();
  QLineEdit *_search, *_title;
  QPlainTextEdit *_text;
  QListWidget *_list;
  QLabel *_status, *_length;
  QPushButton *_new, *_open, *_more, *_save, *_saveAs, *_remove, *_send;
  QTimer _searchTimer;
  LocalNote _note;
  quint64 _queryRevision = 0, _editRevision = 0;
  qint64 _cursor = 0;
  bool _dirty = false, _saving = false, _loading = false, _moreAvailable = false;
  bool _discarding = false;
};
