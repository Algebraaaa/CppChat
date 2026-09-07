#pragma once

#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include "Singleton.h"

class AsioIOServicePool : public Singleton<AsioIOServicePool>
{
	// Singleton 需要调用本类的私有构造函数，因此把它声明为友元。
	friend class Singleton<AsioIOServicePool>;

public:
	~AsioIOServicePool();

	AsioIOServicePool(const AsioIOServicePool&) = delete;
	AsioIOServicePool& operator=(const AsioIOServicePool&) = delete;

	// 按 0、1、0、1……的顺序轮流返回一个 io_context。
	boost::asio::io_context& GetIOService();

	// 停止所有 io_context，并等待所有工作线程退出。
	void Stop();

private:
	using WorkGuard = boost::asio::executor_work_guard<
		boost::asio::io_context::executor_type>;

	// poolSize 是调用构造函数时传进来的临时参数。
	// 当前 Singleton 使用无参构造，因此会采用默认值 2。
	explicit AsioIOServicePool(std::size_t poolSize = 2);

	// 保存实际的池大小。const 表示构造完成后不再改变。
	const std::size_t _poolSize;

	// 下标相同的三个元素是一组：
	// _ioContexts[i] <-> _workGuards[i] <-> _workerThreads[i]
	std::vector<boost::asio::io_context> _ioContexts;
	std::vector<std::unique_ptr<WorkGuard>> _workGuards;
	std::vector<std::thread> _workerThreads;

	// 记录下一次应该返回哪个 io_context。
	std::size_t _nextIOContextIndex;
};
