#pragma once
#include <memory>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

class CServer :public std::enable_shared_from_this<CServer>
{
public:
	// 2026-08-09 修改：端口是小型只读数值，按值传递更直接。
	CServer(boost::asio::io_context& ioc, unsigned short port);
	void Start();
private:
	boost::asio::ip::tcp::acceptor _acceptor; // 监听器
	boost::asio::io_context& _ioc; // 事件循环的引用
	/*
	现在 CServer 的职责缩小了：
	CServer
	├─ 保存 acceptor
	├─ 监听新连接
	└─ 把连接交给 HttpConnection
	不再负责保存某一个客户端 socket
	 tcp::socket _socket;
	 */
};
