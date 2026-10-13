#ifndef SINGLETON_H
#define SINGLETON_H

#include <iostream>
#include <memory>
#include <mutex>

template <typename T> class Singleton {
  // 构造函数是 protected：外面不能直接 new Singleton<T>()，只有子类 T（继承它的类）能调用。
protected:
  Singleton() = default;
  Singleton(const Singleton<T> &) = delete;
  Singleton &operator=(const Singleton<T> &) = delete;
  // 模板类的静态成员——不同的 T（比如 TcpMgr 和 UserMgr）各自拥有独立的一份 _instance，互不干扰
  // C++17特性：用inline可以直接在类内定义并初始化
  inline static std::shared_ptr<T> _instance = nullptr;

public:
  // 第一次有人调用 GetInstance()，才创建那个唯一的实例；
  // 之后再调用，直接返回同一个实例。而且要保证即使多个线程同时第一次调用，也只会创建一个。
  static std::shared_ptr<T> GetInstance()
  {
    // s_flag是static（函数内的静态局部变量，只在第一次进入函数时初始化，之后一直存在）
    // 它能记住"创建实例这件事，到底做过没有"，跨越多次函数调用
    static std::once_flag s_flag;
    // std::call_once 的作用是：配合s_flag 开关，保证第二个参数
    // 在整个程序里最多只被执行一次，而且是线程安全的
    // 多个线程可能同时调用 call_once。call_once 内部会用 s_flag协调：
    // 只让其中一个线程真正去执行那段代码，其它线程在旁边等着，直到那个线程执行完
    // 一旦执行完成，s_flag 就被标记为"已完成"。以后任何线程再调用 call_once(s_flag,
    // ...)，看到标记是"已完成"，就直接跳过、什么都不做
    std::call_once(s_flag, [&]() {
      // 为什么不用make_shared
      _instance = std::shared_ptr<T>(new T);
    });

    return _instance;
  }
  // Meyers 单例
  // static T &getInstance()
  // {
  //     // C++11 标准明确规定：函数内静态局部变量的初始化是线程安全的。
  //     static T instance; // 函数内的静态局部变量
  //     return instance;           // 返回引用
  // }
  void PrintAddress() { std::cout << _instance.get() << std::endl; }
  ~Singleton()
  {
    std::cout << T::staticMetaObject.className() << " singleton destruct" << std::endl;
  }
};

#endif // SINGLETON_H
