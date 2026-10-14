#pragma once
#include <QDialog>

class ClientUpdateService;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

class UpdateDialog : public QDialog {
  Q_OBJECT
public:
  explicit UpdateDialog(ClientUpdateService *service, QWidget *parent = nullptr);
  void checkManually();
signals:
  void installRequested();

protected:
  void reject() override;

private:
  void refresh();
  ClientUpdateService *_service;
  QLabel *_status;
  QPlainTextEdit *_notes;
  QProgressBar *_progress;
  QPushButton *_action;
  QPushButton *_later;
  bool _manual = false;
};
