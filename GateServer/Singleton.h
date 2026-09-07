#pragma once
#include <memory>
#include <mutex>
#include <string_view>
#include <typeinfo>
#include "Logger.h"
template <typename T>
class Singleton {
protected:
	Singleton() = default;
	Singleton(const Singleton<T>&) = delete;
	Singleton& operator=(const Singleton<T>& st) = delete;

	static std::shared_ptr<T> _instance;

	// typeid(T).name() 在 MSVC 下通常返回 "class MysqlMgr" 这样的名称。
	// 去掉 class/struct 前缀后，日志里只显示真正的单例类名。
	static std::string_view SingletonTypeName() noexcept {
		std::string_view type_name(typeid(T).name());
		constexpr std::string_view class_prefix = "class ";
		constexpr std::string_view struct_prefix = "struct ";

		if (type_name.substr(0, class_prefix.size()) == class_prefix) {
			type_name.remove_prefix(class_prefix.size());
		}
		else if (type_name.substr(0, struct_prefix.size()) == struct_prefix) {
			type_name.remove_prefix(struct_prefix.size());
		}
		return type_name;
	}
public:
	static std::shared_ptr<T> GetInstance() {
		static std::once_flag s_flag;
		std::call_once(s_flag, [&]() {
			_instance = std::shared_ptr<T>(new T);
			// 只有 new T 和完整构造都成功后才会执行，因此不会误报初始化成功。
			LOG_INFO(
				SingletonTypeName(),
				" singleton instance initialized successfully.");
			});

		return _instance;
	}

	// 只在 GateServer 关闭阶段、所有工作线程停止后调用。
	// reset 会释放模板持有的 shared_ptr；最后一个引用消失时立即执行具体类析构，
	// 从而保证析构日志发生在 Logger 和 ConfigMgr 这两个迈耶斯单例销毁之前。
	static void DestroyInstance() {
		_instance.reset();
	}

	void PrintAddress() {
		LOG_DEBUG(
			"Singleton instance address: ", static_cast<const void*>(_instance.get()));
	}
	~Singleton() {
		// 这里使用模板参数 T，而不是运行时 this 类型。
		// 即使已经进入基类析构阶段，也仍能准确打印 MysqlMgr、RedisMgr 等名称。
		LOG_INFO(
			SingletonTypeName(),
			" singleton instance destroyed successfully.");
	}
};

template <typename T>
std::shared_ptr<T> Singleton<T>::_instance = nullptr;
