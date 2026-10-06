# spdlog

- 来源：[gabime/spdlog](https://github.com/gabime/spdlog)，固定版本 **v1.15.3**。
- 下载地址：`https://codeload.github.com/gabime/spdlog/zip/refs/tags/v1.15.3`。
- 本次下载压缩包 SHA-256：`b74274c32c8be5dba70b7006c1d41b7d3e5ff0dff8390c8b6390c1189424e094`。
- 仅收录官方 `include/spdlog` 目录，原始头文件未修改。不收录其测试、示例或构建产物。
- `LICENSE` 为 spdlog 的 MIT 许可；内置 fmt 版本为 11.2.0，其许可另存为 `FMT_LICENSE`，原头文件也保留许可声明。

## 客户端接入

`client.pri` 添加 include 路径；`common/logger.cpp` 使用默认的 header-only 模式。
无需安装额外 DLL 或链接外部 fmt/spdlog 二进制，适用于本项目的 MinGW / MSVC Qt Kit。
第三方源码与应用代码分开存放，项目格式化操作不覆盖这些原始头文件。

Qt 日志入口保持不变。业务代码继续使用 `qDebug/qInfo/qWarning/qCritical` 与日志分类。
普通日志使用独立线程池、有界队列；警告及严重错误使用共享的线程安全 sink 即时写入。
接入策略、容量和退出行为见 [性能优化进度](../../docs/performance-improvements.md)。

升级时整体替换 include 目录，对照上游 async logger、daily file sink、periodic worker API，
更新版本、来源校验值和许可证，再进行编译及运行验证。不要直接修改供应方实现。
