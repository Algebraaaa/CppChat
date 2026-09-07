#include "HttpConnection.h"
#include <cassert>
#include <cctype>

#include <boost/core/ignore_unused.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/ostream.hpp>

#include "LogicSystem.h"
#include "Logger.h"

namespace beast = boost::beast;
namespace http = boost::beast::http;
using tcp = boost::asio::ip::tcp;
HttpConnection::HttpConnection(boost::asio::io_context& ioc)
// socket 在构造时绑定 ioc 的 executor；以后移动 socket 也不会改变这个绑定关系。
	: _socket(ioc),
	// 2026-08-09 修改：计时器绑定相同 executor，但此时还不启动倒计时。
	deadline_(_socket.get_executor()) {
}

//开启监听该链接的数据接受请求
void HttpConnection::Start()
{
	auto self = shared_from_this();
	// 2026-08-09 修改：从连接建立、准备读取 HTTP 请求时开始计算 60 秒超时。
	deadline_.expires_after(std::chrono::seconds(60));
	CheckDeadline();

	http::async_read(_socket, _buffer, _request, [self](beast::error_code ec, std::size_t bytes_transferred) {
		try {
			if (ec) {
				self->deadline_.cancel();
				LOG_WARNING("HTTP read failed: ", ec.message());
				return;
			}
			// 表示当前代码故意不使用这个数，避免编译器警告。
			boost::ignore_unused(bytes_transferred);
			self->HandleReq();
		}
		catch (std::exception& exp) {
			self->deadline_.cancel();
			LOG_ERROR("HTTP request handler exception: ", exp.what());
		}
		}
	);
}

//char 转为16进制
//默认输入必须在 0~15 范围内
unsigned char ToHex(unsigned char x)
{
	if (x > 15)return '?';
	return (x < 10) ? static_cast<char>('0' + x) : static_cast<char>('A' + x - 10);
}

//16进制转为char
unsigned char FromHex(unsigned char x)
{
	if (x >= 'A' && x <= 'F')
		return x - 'A' + 10;
	else if (x >= 'a' && x <= 'f')
		return x - 'a' + 10;
	else if (x >= '0' && x <= '9')
		return x - '0';
	else {
		//assert 是断言，意思是我认为程序绝不应该运行到这里；一旦运行到这里，就立刻报错
		assert(false);
		return 0;
	}
}
// 对于汉字，比如“中”在UTF-8中占3个字节：E4 B8 AD
// 所以URL编码结果是：%E4%B8%AD
std::string UrlEncode(const std::string& str)
{
	std::string strTemp = "";
	size_t length = str.length();
	for (size_t i = 0; i < length; i++)
	{
		// 判断是否仅有数字和字母构成
		// isalnum() 表示：is alphabet or number是不是字母或数字
		if (isalnum((unsigned char)str[i]) ||
			(str[i] == '-') ||
			(str[i] == '_') ||
			(str[i] == '.') ||
			(str[i] == '~'))
			// 这些安全字符不需要编码：
			strTemp += str[i];
		// 第二类：空格转换成加号
		else if (str[i] == ' ')
			strTemp += "+";
		// 如果当前字符既不是安全字符，也不是空格，就编码成：
		else
		{
			//其他字符需要提前加%并且高四位和低四位分别转为16进制
			strTemp += '%';
			strTemp += ToHex((unsigned char)str[i] >> 4);
			strTemp += ToHex((unsigned char)str[i] & 0x0F);
		}
	}
	return strTemp;
}
//例如：输入：Tom+Lee%21  输出：Tom Lee!
std::string UrlDecode(const std::string& str)
{
	std::string strTemp = "";
	size_t length = str.length();
	for (size_t i = 0; i < length; i++)
	{
		//还原+为空
		if (str[i] == '+') strTemp += ' ';
		//遇到%将后面的两个字符从16进制转为char再拼接
		else if (str[i] == '%')
		{
			assert(i + 2 < length);
			unsigned char high = FromHex((unsigned char)str[++i]);
			unsigned char low = FromHex((unsigned char)str[++i]);
			strTemp += high * 16 + low;
		}
		else strTemp += str[i];
	}
	return strTemp;
}
//把GET请求的地址拆成“路由路径”和“查询参数”，分别保存到 _get_url 和 _get_params
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
	if (question_mark == std::string::npos) {
		_get_url = target;
		return;
	}
	// 有问号，问号前面是路径
	_get_url = target.substr(0, question_mark);

	// 问号后面是参数部分
	std::string query =
		target.substr(question_mark + 1);

	// 当前参数的开始位置
	size_t start = 0;

	while (start < query.size()) {
		// 查找当前参数后面的&
		size_t ampersand = query.find('&', start);

		// 找到&：参数到&之前结束
		// 没找到&：说明这是最后一个参数，参数到字符串末尾结束
		size_t end =
			ampersand == std::string::npos
			? query.size()
			: ampersand;

		// 截取一个完整参数，例如name=Tom
		std::string parameter =
			query.substr(start, end - start);

		// 查找参数名和参数值之间的=
		size_t equal = parameter.find('=');

		if (equal != std::string::npos) {
			std::string key =
				UrlDecode(
					parameter.substr(0, equal)
				);

			std::string value =
				UrlDecode(
					parameter.substr(equal + 1)
				);

			// 参数名不为空时才保存
			if (!key.empty()) {
				_get_params[key] = value;
			}
		}

		// 移动到下一个参数的开头
		start = end + 1;
	}
}

//处理http请求
void HttpConnection::HandleReq() {
	//设置版本
	_response.version(_request.version());
	//设置为短链接
	_response.keep_alive(false);
	// _response.set() 用来设置 HTTP 响应头。
	// _response.set(响应头名称, 响应头的值);
	_response.set(boost::beast::http::field::access_control_allow_origin, "*");
	// 如果客户端发送的是 GET 请求，就进入大括号处理
	if (_request.method() == http::verb::get)
	{
		PreParseGetParam();
		bool success = LogicSystem::GetInstance()->HandleGet(_get_url, shared_from_this());
		if (!success)
		{
			// 服务器已经收到请求，但是没有找到这个 URL 对应的处理函数
			_response.result(http::status::not_found);
			//它告诉浏览器：响应正文是普通文本，不要把它当HTML网页解析。
			_response.set(http::field::content_type, "text/plain");
			beast::ostream(_response.body()) << "url not found\r\n";
			WriteResponse();
			return;
		}
		// 找到了对应路由
		_response.result(http::status::ok);
		_response.set(http::field::server, "GateServer");
		WriteResponse();
		return;
	}
	if (_request.method() == http::verb::post) {
		bool success = LogicSystem::GetInstance()->HandlePost(_request.target(), shared_from_this());
		if (!success) {
			_response.result(http::status::not_found);
			_response.set(http::field::content_type, "text/plain");
			beast::ostream(_response.body()) << "url not found\r\n";
			WriteResponse();
			return;
		}

		_response.result(http::status::ok);
		_response.set(http::field::server, "GateServer");
		WriteResponse();
		return;
	}
}

void HttpConnection::CheckDeadline() {
	auto self = shared_from_this();

	deadline_.async_wait(
		[self](beast::error_code ec)
		{
			if (!ec)
			{
				// 这个客户端处理时间太长，不再继续等待，直接断开连接。
				beast::error_code closeError;
				self->_socket.close(closeError);
			}
		});
}

// 把已经准备好的_response，通过当前客户端的_socket发送出去。
void HttpConnection::WriteResponse() {
	auto self = shared_from_this();
	// _response.body获得响应正文
	// _response.body().size获取响应正文的字节数
	_response.content_length(_response.body().size());

	http::async_write(
		_socket,
		_response,
		// 发送完成后执行的回调函数
		// std::size_t 表示实际发送了多少字节
		[self](beast::error_code ec, std::size_t)
		{
			// 使用短连接：一个请求处理完并返回响应以后，不继续使用这个连接
			self->_socket.shutdown(tcp::socket::shutdown_send, ec);
			// 取消超时计时器
			self->deadline_.cancel();
		});
}
