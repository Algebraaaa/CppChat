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

class LoginDialog : public QDialog {
  Q_OBJECT

public:
  explicit LoginDialog(QWidget *parent = nullptr);
  ~LoginDialog();
  void ResetForLogin(const QString &message = {});

private:
  void initHttpHandlers();
  void initHead();
  Ui::LoginDialog *ui;
  bool checkEmailValid();
  void AddTipErr(TipError te, QString tips);
  void DelTipErr(TipError te);
  QMap<TipError, QString> _tipErrors;
  QMap<ReqId, std::function<void(const QJsonObject &)>> _handlers;
  void showTip(QString str, bool b_ok);
  bool checkPwdValid();
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
