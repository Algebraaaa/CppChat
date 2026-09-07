#include "AsioIOServicePool.h"

#include "Logger.h"

AsioIOServicePool::AsioIOServicePool(std::size_t pool_size)
	: pool_size_(pool_size == 0 ? 1 : pool_size),
	  io_contexts_(pool_size_),
	  work_guards_(pool_size_),
	  next_io_context_index_(0)
{
	for (std::size_t index = 0; index < pool_size_; ++index)
	{
		work_guards_[index] = std::make_unique<WorkGuard>(
			io_contexts_[index].get_executor());
	}

	worker_threads_.reserve(pool_size_);
	for (std::size_t index = 0; index < pool_size_; ++index)
	{
		worker_threads_.emplace_back([this, index]()
		{
			io_contexts_[index].run();
		});
	}

	LOG_INFO("Asio IO service pool started with ", pool_size_, " workers.");
}

AsioIOServicePool::~AsioIOServicePool()
{
	Stop();
}

boost::asio::io_context& AsioIOServicePool::GetIOService()
{
	boost::asio::io_context& selected = io_contexts_[next_io_context_index_];
	next_io_context_index_ = (next_io_context_index_ + 1) % pool_size_;
	return selected;
}

void AsioIOServicePool::Stop()
{
	std::lock_guard<std::mutex> lock(stop_mutex_);
	if (stopped_)
	{
		return;
	}
	stopped_ = true;

	for (auto& io_context : io_contexts_)
	{
		io_context.stop();
	}
	for (auto& work_guard : work_guards_)
	{
		work_guard.reset();
	}
	for (auto& worker_thread : worker_threads_)
	{
		if (worker_thread.joinable())
		{
			worker_thread.join();
		}
	}

	LOG_INFO("Asio IO service pool stopped.");
}
