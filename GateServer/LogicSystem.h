#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "Singleton.h"

class HttpConnection;
// 把 std::function<void(std::shared_ptr<HttpConnection>)> 
// 这个很长的类型，起一个短名字叫 HttpHandler。
// HttpHandler handler;就等价于：
// std::function<void(std::shared_ptr<HttpConnection>)> handler;
typedef std::function<void(std::shared_ptr<HttpConnection>)> HttpHandler;
class LogicSystem :public Singleton<LogicSystem>
{
	friend class Singleton<LogicSystem>;
public:
	~LogicSystem();
	// 查找并执行路由
	bool HandleGet(std::string path, std::shared_ptr<HttpConnection>con);
	bool HandlePost(std::string path, std::shared_ptr<HttpConnection>con);
	// 注册路由
	void RegGet(std::string url, HttpHandler handler);
	void RegPost(std::string url, HttpHandler handler);
private:
	LogicSystem();
	std::map<std::string, HttpHandler> _post_handlers;
	std::map<std::string, HttpHandler> _get_handlers;
};
