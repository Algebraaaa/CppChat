#ifndef RESETDIALOG_H
#define RESETDIALOG_H

#include "common/global.h"
#include <QByteArray>
#include <QDialog>
#include <QMap>
#include <QString>

namespace Ui {
class ResetDialog;
}
class FormFeedback;

class ResetDialog : public QDialog {
  Q_OBJECT

public:
  explicit ResetDialog(QWidget *parent = nullptr);
  ~ResetDialog();

private:
  void initHttpHandlers();
  void showTip(const QString &message, bool ok, ReqId id = ReqId::ID_RESET_PWD);
  void resetPage();

  Ui::ResetDialog *ui;
  FormFeedback *_feedback = nullptr;
  QMap<ReqId, std::function<void(const QJsonObject &)>> _handlers;
signals:
  void switchLogin();
private slots:
  void on_cancel_btn_clicked();
  void on_confirm_btn_clicked();
  void on_get_verify_btn_clicked();

  void slot_reset_mod_finish(ReqId id, QByteArray data, ErrorCodes err);
};

#endif // RESETDIALOG_H
