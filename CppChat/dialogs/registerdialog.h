#ifndef REGISTERDIALOG_H
#define REGISTERDIALOG_H
#include "common/global.h"
#include <QDialog>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QTimer>
#include <functional>

namespace Ui {
class RegisterDialog;
}

class RegisterDialog : public QDialog {
  Q_OBJECT

public:
  explicit RegisterDialog(QWidget *parent = nullptr);
  ~RegisterDialog();

private slots:
  void on_get_code_clicked();
  void on_cancel_btn_clicked();
  void on_confirm_btn_clicked();
  void slot_reg_mod_finish(ReqId id, QString res, ErrorCodes err);
  void ChangeTipPage();
  void on_return_btn_clicked();

private:
  void initHttpHandlers();
  void showTip(QString str, bool b_ok);
  void addTipError(TipError error, const QString &message);
  void removeTipError(TipError error);
  bool checkUserValid(bool focusOnError = false);
  bool checkEmailValid(bool focusOnError = false);
  bool checkPasswordValid(bool focusOnError = false);
  bool checkConfirmPasswordValid(bool focusOnError = false);
  bool checkVerifyCodeValid(bool focusOnError = false);
  void resetRegistrationPage();
  void returnToLogin();
  Ui::RegisterDialog *ui;
  QMap<ReqId, std::function<void(const QJsonObject &)>> _handlers;
  QMap<TipError, QString> _tipErrors;

  QTimer *_countdown_timer = nullptr;
  int _countdown = 5;
signals:
  void switchLogin();
};

#endif // REGISTERDIALOG_H
