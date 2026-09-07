#include "AsioIOServicePool.h"

#include "Logger.h"

AsioIOServicePool::AsioIOServicePool(std::size_t poolSize)
    // poolSize 是构造函数参数，可以直接用来初始化成员。
    // 至少创建一个 io_context，避免大小为 0 时访问数组越界。
    : _poolSize(poolSize == 0 ? 1 : poolSize),
      _ioContexts(_poolSize),
      _workGuards(_poolSize),
      _nextIOContextIndex(0)
{
    // 为每一个 io_context 创建工作守卫。
    // 工作守卫保证暂时没有网络任务时，run() 仍然继续等待。
    for (std::size_t index = 0; index < _poolSize; ++index)
    {
        boost::asio::io_context::executor_type executor =
            _ioContexts[index].get_executor();

        _workGuards[index] = std::make_unique<WorkGuard>(executor);
    }

    // 提前预留空间，避免添加线程时 vector 多次扩容。
    _workerThreads.reserve(_poolSize);

    // 每一个 io_context 配一个工作线程。
    for (std::size_t index = 0; index < _poolSize; ++index)
    {
        _workerThreads.emplace_back([this, index]()
        {
         // 线程池构造时，工作线程马上调用 run()，但这时：
         // 还没有 socket；还没有 async_read；还没有 async_write；还没有 async_accept；
            _ioContexts[index].run();
        });
    }

	LOG_INFO(
		"Asio I/O worker pool initialized successfully: workers=",
		_poolSize);
}

AsioIOServicePool::~AsioIOServicePool()
{
    Stop();
	// 具体单例名称由 Singleton<AsioIOServicePool> 的析构函数统一打印。
}

boost::asio::io_context& AsioIOServicePool::GetIOService()
{
    // 先选中当前下标对应的 io_context。
    boost::asio::io_context& selectedIOContext =
        _ioContexts[_nextIOContextIndex];

    // 再把下标移动到下一个位置。
    ++_nextIOContextIndex;

    // 到达数组末尾后，从第 0 个重新开始。
    if (_nextIOContextIndex >= _poolSize)
    {
        _nextIOContextIndex = 0;
    }

    return selectedIOContext;
}

void AsioIOServicePool::Stop()
{
    // stop() 让正在 run() 的事件循环尽快返回。
    for (boost::asio::io_context& ioContext : _ioContexts)
    {
        ioContext.stop();
    }

    // 销毁工作守卫，表示以后不再要求 io_context 继续等待新任务。
    for (std::unique_ptr<WorkGuard>& workGuard : _workGuards)
    {
        workGuard.reset();
    }

    // 等待所有工作线程真正退出后，Stop() 才返回。
    for (std::thread& workerThread : _workerThreads)
    {
        if (workerThread.joinable())
        {
            workerThread.join();
        }
    }
}
