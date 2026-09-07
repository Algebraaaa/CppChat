#ifndef RESETDIALOG_H
#define RESETDIALOG_H

#include "common/global.h"
#include <QDialog>
#include <QMap>
#include <QString>

namespace Ui {
class ResetDialog;
}

class ResetDialog : public QDialog {
  Q_OBJECT

public:
  explicit ResetDialog(QWidget *parent = nullptr);
  ~ResetDialog();

private:
  void initHttpHandlers();
  void showTip(const QString &message, bool ok);
  void addTipError(TipError error, const QString &message);
  void removeTipError(TipError error);
  bool checkUserValid(bool focusOnError = false);
  bool checkEmailValid(bool focusOnError = false);
  bool checkPasswordValid(bool focusOnError = false);
  bool checkVerifyCodeValid(bool focusOnError = false);
  void resetPage();

  Ui::ResetDialog *ui;
  QMap<TipError, QString> _tipErrors;
  QMap<ReqId, std::function<void(const QJsonObject &)>> _handlers;
signals:
  void switchLogin();
private slots:
  void on_cancel_btn_clicked();
  void on_confirm_btn_clicked();
  void on_get_verify_btn_clicked();

  void slot_reset_mod_finish(ReqId id, QString res, ErrorCodes err);
};

#endif // RESETDIALOG_H
