#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include "common/global.h"
#include <QByteArray>
#include <QDialog>
#include <QJsonObject>
#include <QMap>
#include <functional>
namespace Ui {
class LoginDialog;
}
class FormFeedback;
class QCheckBox;
class QPushButton;

class LoginDialog : public QDialog {
  Q_OBJECT

public:
  explicit LoginDialog(QWidget *parent = nullptr);
  ~LoginDialog();
  void ResetForLogin(const QString &message = {}, bool forgetSaved = false);
  void TryAutoLogin();
  bool ForgetAutoLogin();

private:
  void initHttpHandlers();
  void initHead();
  Ui::LoginDialog *ui;
  FormFeedback *_feedback = nullptr;
  QCheckBox *_autoLogin = nullptr;
  QPushButton *_cancelAutoLogin = nullptr;
  bool _pending = false;
  bool _automatic = false;
  bool _autoAttempted = false;
  QString _pendingEmail;
  QString _pendingPassword;
  QMap<ReqId, std::function<void(const QJsonObject &)>> _handlers;
  void showTip(QString str, bool b_ok);
  bool enableBtn(bool enabled);
  void slot_tcp_con_finish(bool bsuccess);
  void slot_login_failed(int error);
  int _uid = 0;
  QString _token;
signals:
  void switchRegister();
  void switchReset();
  void sig_connect_tcp(ServerInfo);
private slots:
  void on_reg_btn_clicked();
  void slot_forget_pwd();
  void on_login_btn_clicked();
  void slot_login_mod_finish(ReqId id, QByteArray data, ErrorCodes err);
};

#endif // LOGINDIALOG_H
