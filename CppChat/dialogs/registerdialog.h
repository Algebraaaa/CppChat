#ifndef REGISTERDIALOG_H
#define REGISTERDIALOG_H
#include "common/global.h"
#include <QByteArray>
#include <QDialog>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QTimer>
#include <functional>

namespace Ui {
class RegisterDialog;
}
class FormFeedback;

class RegisterDialog : public QDialog {
  Q_OBJECT

public:
  explicit RegisterDialog(QWidget *parent = nullptr);
  ~RegisterDialog();

private slots:
  void on_get_code_clicked();
  void on_cancel_btn_clicked();
  void on_confirm_btn_clicked();
  void slot_reg_mod_finish(ReqId id, QByteArray data, ErrorCodes err);
  void ChangeTipPage();
  void on_return_btn_clicked();

private:
  void initHttpHandlers();
  void showTip(const QString &message, bool ok, ReqId id = ReqId::ID_REG_USER);
  void resetRegistrationPage();
  void returnToLogin();
  Ui::RegisterDialog *ui;
  QMap<ReqId, std::function<void(const QJsonObject &)>> _handlers;
  FormFeedback *_feedback = nullptr;

  QTimer *_countdown_timer = nullptr;
  int _countdown = 5;
signals:
  void switchLogin();
};

#endif // REGISTERDIALOG_H
