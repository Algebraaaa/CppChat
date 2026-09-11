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
	friend class Singleton<AsioIOServicePool>;

public:
	~AsioIOServicePool();

	AsioIOServicePool(const AsioIOServicePool&) = delete;
	AsioIOServicePool& operator=(const AsioIOServicePool&) = delete;

	boost::asio::io_context& GetIOService();
	void Stop();

private:
	using WorkGuard = boost::asio::executor_work_guard<
		boost::asio::io_context::executor_type>;

	explicit AsioIOServicePool(std::size_t pool_size = 2);

	const std::size_t pool_size_;
	std::vector<boost::asio::io_context> io_contexts_;
	std::vector<std::unique_ptr<WorkGuard>> work_guards_;
	std::vector<std::thread> worker_threads_;
	std::size_t next_io_context_index_;
	std::mutex stop_mutex_;
	bool stopped_ = false;
};
