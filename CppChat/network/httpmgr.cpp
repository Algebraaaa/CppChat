#include "network/httpmgr.h"
#include <QByteArray>
#include <QDebug>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>

HttpMgr::~HttpMgr() {}

HttpMgr::HttpMgr()
{
  connect(this, &HttpMgr::sig_http_finish, this, &HttpMgr::slot_http_finish);
}

void HttpMgr::PostHttpReq(QUrl url, QJsonObject json, ReqId req_id, Modules mod)
{
  // 测试阶段记录请求去向和字段名称，不输出密码、验证码等字段值。
  qDebug() << "HTTP POST"
           << "url:" << url
           << "reqId:" << static_cast<int>(req_id)
           << "module:" << static_cast<int>(mod)
           << "payloadKeys:" << json.keys();

  QByteArray data = QJsonDocument(json).toJson(); // ① 把 JSON 对象转成能发送的字节
  QNetworkRequest request(url);                   // ② 创建一个"请求",指定发到哪个网址
  // 第一行想说"我发的是 JSON 格式
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  // 第二行想说"数据长度是多少"
  request.setHeader(QNetworkRequest::ContentLengthHeader, data.length());
  // ④ 拿到指向自己的 shared_ptr（关键）
  auto self = shared_from_this(); // 它返回一个指向自己的shared_ptr，
                                  // 在这里它指向的就是那个全局唯一的 HttpMgr 实例
  // ⑤ 让快递员真正把请求发出去
  QNetworkReply *reply = _manager.post(request, data);
  // ⑥ 约定"回信到了"时干什么
  // lambda为什么不捕获this？等回信期间对象若被销毁，this 变野指针，emit this->... 崩溃
  QObject::connect(reply, &QNetworkReply::finished, [self, reply, req_id, mod]() {
    if (reply->error() != QNetworkReply::NoError) { // 出错了
      qWarning() << "HTTP request failed:"
                 << "reqId:" << static_cast<int>(req_id)
                 << "module:" << static_cast<int>(mod)
                 << "error:" << reply->errorString()
                 << "url:" << reply->url();
      // 发送信号通知完成
      emit self->sig_http_finish(req_id, "", ErrorCodes::ERR_NETWORK, mod);
      reply->deleteLater();
      return;
    }
    // 无错误
    QString res = reply->readAll(); // 没出错，读出服务器返回的内容
    qDebug() << "HTTP response received"
             << "reqId:" << static_cast<int>(req_id)
             << "module:" << static_cast<int>(mod)
             << "bytes:" << res.toUtf8().size()
             << "url:" << reply->url();
    // 发送信号通知完成
    emit self->sig_http_finish(req_id, res, ErrorCodes::SUCCESS, mod);
    reply->deleteLater();
    return;
  });
}

void HttpMgr::slot_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod)
{
  if (mod == Modules::REGISTERMOD) {
    // 发送信号通知指定模块http的响应结束了
    emit sig_reg_mod_finish(id, res, err);
  } else if (mod == Modules::RESETMOD) {
    emit sig_reset_mod_finish(id, res, err);
  } else if (mod == Modules::LOGINMOD) {
    emit sig_login_mod_finish(id, res, err);
  }
}
