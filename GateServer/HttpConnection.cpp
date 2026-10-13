#include "HttpConnection.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>

#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/ostream.hpp>

#include "ConfigMgr.h"
#include "Logger.h"
#include "LogicSystem.h"

namespace beast = boost::beast;
namespace http = boost::beast::http;
using tcp = boost::asio::ip::tcp;
namespace
{
std::atomic_uint64_t next_connection{0};
}
HttpConnection::HttpConnection(boost::asio::io_context &ioc)
    // socket 在构造时绑定 ioc 的 executor；以后移动 socket
    // 也不会改变这个绑定关系。
    : strand_(boost::asio::make_strand(ioc)), _socket(strand_),
      // 2026-08-09 修改：计时器绑定相同 executor，但此时还不启动倒计时。
      deadline_(_socket.get_executor()), connection_key_(std::to_string(++next_connection))
{
    parser_.header_limit(8192);
    parser_.body_limit(32 * 1024);
    try
    {
        const auto text =
            ConfigMgr::GetInstance()["GateServer"].GetValue("RequestTimeoutSeconds", "60");
        std::size_t consumed = 0;
        const auto seconds = std::stoul(text, &consumed);
        if (consumed != text.size() || seconds < 1 || seconds > 300)
            throw std::out_of_range("RequestTimeoutSeconds");
        timeout_seconds_ = static_cast<unsigned int>(seconds);
    }
    catch (const std::exception &)
    {
        LOG_WARNING("Invalid GateServer.RequestTimeoutSeconds; using 60.");
    }
}

// 开启监听该链接的数据接受请求
void HttpConnection::Start()
{
    auto self = shared_from_this();
    boost::asio::dispatch(
        strand_,
        [self]
        {
            self->deadline_.expires_after(std::chrono::seconds(self->timeout_seconds_));
            self->CheckDeadline();
            http::async_read(self->_socket, self->_buffer, self->parser_,
                             boost::asio::bind_executor(
                                 self->strand_,
                                 [self](beast::error_code ec, std::size_t)
                                 {
                                     try
                                     {
                                         if (ec)
                                         {
                                             if (ec == http::error::body_limit)
                                             {
                                                 self->SendError(http::status::payload_too_large,
                                                                 "request body too large");
                                                 self->WriteResponse();
                                             }
                                             else
                                             {
                                                 self->Close();
                                                 LOG_WARNING("HTTP read failed: ", ec.message());
                                             }
                                             return;
                                         }
                                         if (self->cancelled_)
                                             return;
                                         if (!self->accepting_requests_)
                                         {
                                             self->Close();
                                             return;
                                         }
                                         self->_request = self->parser_.release();
                                         self->request_submitted_ =
                                             LogicSystem::GetInstance()->SubmitRequest(self);
                                         if (!self->request_submitted_)
                                         {
                                             self->SendError(http::status::service_unavailable,
                                                             "server busy, retry later");
                                             self->WriteResponse();
                                         }
                                     }
                                     catch (const std::exception &exp)
                                     {
                                         self->Close();
                                         LOG_ERROR("HTTP request dispatch exception: ", exp.what());
                                     }
                                 }));
        });
}

// char 转为16进制
// 默认输入必须在 0~15 范围内
unsigned char ToHex(unsigned char x)
{
    if (x > 15)
        return '?';
    return (x < 10) ? static_cast<char>('0' + x) : static_cast<char>('A' + x - 10);
}

// 16进制转为char
unsigned char FromHex(unsigned char x)
{
    if (x >= 'A' && x <= 'F')
        return x - 'A' + 10;
    else if (x >= 'a' && x <= 'f')
        return x - 'a' + 10;
    else if (x >= '0' && x <= '9')
        return x - '0';
    else
    {
        throw std::invalid_argument("invalid URL escape");
    }
}
// 对于汉字，比如“中”在UTF-8中占3个字节：E4 B8 AD
// 所以URL编码结果是：%E4%B8%AD
std::string UrlEncode(const std::string &str)
{
    std::string strTemp = "";
    size_t length = str.length();
    for (size_t i = 0; i < length; i++)
    {
        // 判断是否仅有数字和字母构成
        // isalnum() 表示：is alphabet or number是不是字母或数字
        if (isalnum((unsigned char)str[i]) || (str[i] == '-') || (str[i] == '_') ||
            (str[i] == '.') || (str[i] == '~'))
            // 这些安全字符不需要编码：
            strTemp += str[i];
        // 第二类：空格转换成加号
        else if (str[i] == ' ')
            strTemp += "+";
        // 如果当前字符既不是安全字符，也不是空格，就编码成：
        else
        {
            // 其他字符需要提前加%并且高四位和低四位分别转为16进制
            strTemp += '%';
            strTemp += ToHex((unsigned char)str[i] >> 4);
            strTemp += ToHex((unsigned char)str[i] & 0x0F);
        }
    }
    return strTemp;
}
// 例如：输入：Tom+Lee%21  输出：Tom Lee!
std::string UrlDecode(const std::string &str)
{
    std::string strTemp = "";
    size_t length = str.length();
    for (size_t i = 0; i < length; i++)
    {
        // 还原+为空
        if (str[i] == '+')
            strTemp += ' ';
        // 遇到%将后面的两个字符从16进制转为char再拼接
        else if (str[i] == '%')
        {
            if (i + 2 >= length)
                throw std::invalid_argument("incomplete URL escape");
            unsigned char high = FromHex((unsigned char)str[++i]);
            unsigned char low = FromHex((unsigned char)str[++i]);
            strTemp += high * 16 + low;
        }
        else
            strTemp += str[i];
    }
    return strTemp;
}
// 把GET请求的地址拆成“路由路径”和“查询参数”，分别保存到 _get_url 和 _get_params
void HttpConnection::PreParseGetParam()
{
    // 防止当前对象中残留上一次解析结果
    _get_url.clear();
    _get_params.clear();
    // 例如：/get_test?name=Tom&age=18
    std::string target = _request.target();
    // 找到路径和参数之间的问号
    size_t question_mark = target.find('?');
    // 没有问号，整个target就是路径
    if (question_mark == std::string::npos)
    {
        _get_url = target;
        return;
    }
    // 有问号，问号前面是路径
    _get_url = target.substr(0, question_mark);

    // 问号后面是参数部分
    std::string query = target.substr(question_mark + 1);

    // 当前参数的开始位置
    size_t start = 0;

    while (start < query.size())
    {
        // 查找当前参数后面的&
        size_t ampersand = query.find('&', start);

        // 找到&：参数到&之前结束
        // 没找到&：说明这是最后一个参数，参数到字符串末尾结束
        size_t end = ampersand == std::string::npos ? query.size() : ampersand;

        // 截取一个完整参数，例如name=Tom
        std::string parameter = query.substr(start, end - start);

        // 查找参数名和参数值之间的=
        size_t equal = parameter.find('=');

        if (equal != std::string::npos)
        {
            std::string key = UrlDecode(parameter.substr(0, equal));

            std::string value = UrlDecode(parameter.substr(equal + 1));

            // 参数名不为空时才保存
            if (!key.empty())
            {
                _get_params[key] = value;
            }
        }

        // 移动到下一个参数的开头
        start = end + 1;
    }
}

// 处理http请求
void HttpConnection::HandleReq()
{
    if (cancelled_)
        return; // Expired queued requests must not start business side effects.
    try
    {
        _response.version(_request.version());
        _response.keep_alive(false);
        _response.set(http::field::access_control_allow_origin, "*");
        bool matched = false;
        if (_request.method() == http::verb::get)
        {
            PreParseGetParam();
            matched = LogicSystem::GetInstance()->HandleGet(_get_url, shared_from_this());
        }
        else if (_request.method() == http::verb::post)
        {
            matched = LogicSystem::GetInstance()->HandlePost(_request.target(), shared_from_this());
        }
        if (_request.method() != http::verb::get && _request.method() != http::verb::post)
        {
            SendError(http::status::method_not_allowed, "method not allowed");
        }
        else if (matched)
        {
            _response.result(http::status::ok);
            _response.set(http::field::server, "GateServer");
        }
        else
        {
            _response.result(http::status::not_found);
            _response.set(http::field::content_type, "text/plain");
            beast::ostream(_response.body()) << "url not found\r\n";
        }
    }
    catch (const std::invalid_argument &)
    {
        SendError(http::status::bad_request, "invalid request");
    }
    catch (const std::exception &error)
    {
        LOG_ERROR("HTTP business handler exception: ", error.what());
        SendError(http::status::internal_server_error, "internal server error");
    }
    auto self = shared_from_this();
    boost::asio::post(strand_,
                      [self]
                      {
                          if (!self->cancelled_)
                              self->WriteResponse();
                      });
}

void HttpConnection::SendError(http::status status, const char *message)
{
    _response = {};
    _response.version(_request.version());
    _response.keep_alive(false);
    _response.result(status);
    _response.set(http::field::server, "GateServer");
    _response.set(http::field::content_type, "application/json");
    _response.set(http::field::access_control_allow_origin, "*");
    beast::ostream(_response.body()) << "{\"error\":" << static_cast<unsigned int>(status)
                                     << ",\"message\":\"" << message << "\"}";
}

void HttpConnection::Close()
{
    if (socket_closed_)
        return;
    socket_closed_ = true;
    cancelled_ = true;
    deadline_.cancel();
    beast::error_code ignored;
    _socket.shutdown(tcp::socket::shutdown_both, ignored);
    _socket.close(ignored);
    if (on_closed_)
        on_closed_();
    auto handlers = std::move(drain_handlers_);
    for (auto &handler : handlers)
        handler();
}

void HttpConnection::Abort()
{
    auto self = shared_from_this();
    boost::asio::dispatch(strand_, [self] { self->Close(); });
}

void HttpConnection::Quiesce()
{
    accepting_requests_ = false;
    auto self = shared_from_this();
    boost::asio::post(strand_,
                      [self]
                      {
                          if (!self->request_submitted_ && !self->response_started_)
                              self->Close(); // Idle/incomplete reads are not accepted business.
                      });
}

void HttpConnection::DrainAndClose(std::chrono::seconds timeout, std::function<void()> callback)
{
    auto self = shared_from_this();
    boost::asio::post(strand_,
                      [self, timeout, callback = std::move(callback)]
                      {
                          if (self->socket_closed_)
                          {
                              callback();
                              return;
                          }
                          self->drain_handlers_.push_back(callback);
                          const auto deadline = std::chrono::steady_clock::now() + timeout;
                          if (self->deadline_.expiry() > deadline)
                          {
                              self->deadline_.expires_at(deadline);
                              self->CheckDeadline();
                          }
                          if (!self->request_submitted_ && !self->response_started_)
                              self->Close();
                      });
}

void HttpConnection::FinishResponse()
{
    // Closing with unread request bytes can send RST and erase the response at
    // the peer. Send FIN first, then drain within strict time/byte bounds.
    cancelled_ = true;
    beast::error_code error;
    _socket.shutdown(tcp::socket::shutdown_send, error);
    if (error)
    {
        Close();
        return;
    }
    deadline_.expires_at(
        std::min(deadline_.expiry(), std::chrono::steady_clock::now() + std::chrono::seconds(1)));
    CheckDeadline();
    DrainInput();
}

void HttpConnection::DrainInput()
{
    auto self = shared_from_this();
    _socket.async_read_some(
        boost::asio::buffer(drain_buffer_.data(), std::min(drain_buffer_.size(), drain_remaining_)),
        boost::asio::bind_executor(strand_,
                                   [self](beast::error_code error, std::size_t bytes)
                                   {
                                       if (error || bytes >= self->drain_remaining_)
                                       {
                                           self->Close();
                                           return;
                                       }
                                       self->drain_remaining_ -= bytes;
                                       self->DrainInput();
                                   }));
}

void HttpConnection::CheckDeadline()
{
    auto self = shared_from_this();

    deadline_.async_wait(boost::asio::bind_executor(strand_,
                                                    [self](beast::error_code ec)
                                                    {
                                                        if (!ec)
                                                        {
                                                            // 这个客户端处理时间太长，不再继续等待，直接断开连接。
                                                            self->Close();
                                                        }
                                                    }));
}

// 把已经准备好的_response，通过当前客户端的_socket发送出去。
void HttpConnection::WriteResponse()
{
    if (cancelled_)
        return;
    response_started_ = true;
    auto self = shared_from_this();
    // _response.body获得响应正文
    // _response.body().size获取响应正文的字节数
    _response.content_length(_response.body().size());

    http::async_write(
        _socket, _response,
        // 发送完成后执行的回调函数
        // std::size_t 表示实际发送了多少字节
        boost::asio::bind_executor(strand_,
                                   [self](beast::error_code ec, std::size_t)
                                   {
                                       // 使用短连接：一个请求处理完并返回响应以后，不继续使用这个连接
                                       if (ec)
                                       {
                                           if (ec != boost::asio::error::operation_aborted)
                                               LOG_WARNING("HTTP write failed: ", ec.message());
                                           self->Close();
                                       }
                                       else
                                       {
                                           self->FinishResponse();
                                       }
                                   }));
}
