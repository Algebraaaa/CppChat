#ifndef HTTPMGR_H
#define HTTPMGR_H
#include "common/singleton.h"
#include "common/global.h"
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>
#include <memory>
// CRTP
class HttpMgr : public QObject,
                public Singleton<HttpMgr>,
                public std::enable_shared_from_this<HttpMgr> {
  Q_OBJECT
public:
  void PostHttpReq(QUrl url, QJsonObject json, ReqId req_id, Modules mod);
  ~HttpMgr();

private:
  friend class Singleton<HttpMgr>;
  HttpMgr();
  // 这是Qt自带的网络类
  // 可以理解成"快递员"——你把要寄的东西（请求）交给它，它负责送出去、把回执带回来
  QNetworkAccessManager _manager;
  // 封装"发一个 POST 请求"的动作

private slots:
  void slot_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod);
signals:
  // id：这次是哪种请求（获取验证码？还是注册？
  // res：服务器返回的内容（一段文本）
  // err：成功还是出错了
  // mod：哪个模块发的
  void sig_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod);
  void sig_reg_mod_finish(ReqId id, QString res, ErrorCodes err);
  void sig_reset_mod_finish(ReqId id, QString res, ErrorCodes err);
  void sig_login_mod_finish(ReqId id, QString res, ErrorCodes err);
};

#endif // HTTPMGR_H
