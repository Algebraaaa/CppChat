#include "CServer.h"
#include <boost/system/error_code.hpp>
#include "HttpConnection.h"
#include "AsioIOServicePool.h"
#include "Logger.h"
// 对于类的成员是引用类型，需要通过初始化列表进行初始化
CServer::CServer(boost::asio::io_context& ioc, unsigned short port) :
	_ioc(ioc),
	_acceptor(ioc, boost::asio::ip::tcp::endpoint(
		boost::asio::ip::tcp::v4(), port)){}

void CServer::Start()
{
	auto self = shared_from_this();

	/*
	 * 旧版流程：
	 * 1. CServer 用主 io_context 创建 _socket；
	 * 2. accept 成功后，再用 std::move(_socket) 把 socket 交给 HttpConnection。
	 *
	 * std::move 只转移 socket 的所有权，不会更换 socket 内部绑定的 executor。
	 * 因此旧版 socket 即使被移动到 HttpConnection，后续异步读写仍由主 io_context 调度，
	 * 并没有真正把连接分配给工作线程。
	 *
	 * 当前流程：
	 * 1. 先从 AsioIOServicePool 选择一个工作 io_context；
	 * 2. HttpConnection 直接使用这个工作 io_context 创建自己的 socket；
	 * 3. acceptor 把新连接接收到这个 socket 中。
	 *
	 * 这样 socket 从出生时就属于工作 io_context，后续 async_read/async_write
	 * 才会由对应的工作线程执行，这才是真正的线程分流。
	 */
	auto& io_context = AsioIOServicePool::GetInstance()->GetIOService();
	std::shared_ptr<HttpConnection>new_con = std::make_shared<HttpConnection>(io_context);
	_acceptor.async_accept(new_con->Getsocket(), [self,new_con](boost::system::error_code ec) {
		try {
			//出错则放弃这个连接，继续监听新链接
			if (ec) {
				self->Start();
				return;
			}
			// new_con 从创建时就拥有这个 socket，不再需要从 CServer 移动 socket。
			// 开始异步读取这个客户端发来的 HTTP 请求。
			new_con->Start();
			//继续监听
			self->Start();
		}
		// 接住异常对象，并把它命名为 exp
		catch (std::exception& exp) {
			LOG_ERROR("CServer accept handler exception: ", exp.what());
			self->Start();
		}
		});
}
