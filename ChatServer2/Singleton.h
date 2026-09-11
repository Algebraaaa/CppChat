#pragma once

#include <memory>
#include <mutex>
#include <string_view>
#include <typeinfo>

#include "Logger.h"

template <typename T>
class Singleton
{
protected:
	Singleton() = default;
	Singleton(const Singleton<T>&) = delete;
	Singleton& operator=(const Singleton<T>&) = delete;

	static std::shared_ptr<T> instance_;

	static std::string_view SingletonTypeName() noexcept
	{
		std::string_view type_name(typeid(T).name());
		constexpr std::string_view class_prefix = "class ";
		constexpr std::string_view struct_prefix = "struct ";

		if (type_name.substr(0, class_prefix.size()) == class_prefix)
		{
			type_name.remove_prefix(class_prefix.size());
		}
		else if (type_name.substr(0, struct_prefix.size()) == struct_prefix)
		{
			type_name.remove_prefix(struct_prefix.size());
		}
		return type_name;
	}

public:
	static std::shared_ptr<T> GetInstance()
	{
		static std::once_flag flag;
		std::call_once(flag, []()
		{
			instance_ = std::shared_ptr<T>(new T);
		});
		return instance_;
	}

	// 在业务线程全部停止后显式调用，让具体单例在 Logger 销毁前释放。
	static void DestroyInstance()
	{
		instance_.reset();
	}

	virtual ~Singleton()
	{
		// 使用模板参数 T，即使已经进入基类析构阶段也能打印具体单例类名。
		LOG_DEBUG(SingletonTypeName(), " singleton instance destroyed.");
	}
};

template <typename T>
std::shared_ptr<T> Singleton<T>::instance_ = nullptr;
