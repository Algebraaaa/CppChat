# CppChat

B站恋恋风辰大佬的C++全栈即时通讯项目，我在辰哥的基础上做了改良，给服务端和客户端加了日志类，做了一些美化，以及加了很多个人的注释

版本更新与历史开发、验证记录已移至 [Releases](https://github.com/Algebraaaa/CppChat/releases)；安装包见 [最新发布](https://github.com/Algebraaaa/CppChat/releases/latest)。

最近更新：**2026-10-23**。新增 [Android QML 客户端](CppChatAndroid/README.md)（0.1.20，ARM64 / Android 9+），
复用桌面公共业务模块；支持桌面与手机同时在线、账号记忆/自动登录、断线恢复、本地记录与独立安卓更新检测，
以及文件、语音消息和语音/视频通话。包含配套设备会话、资源权限及通知同步的服务端修改；普通页面不展示服务地址。
真实 TLS 协议联调与合成媒体验证已完成，Android 真机安装、Keystore、权限与音视频体验仍需设备验收；厂商推送未接通。
Windows 已发布版本与 Android 版本分别维护，不因新增 Android 源码改变 Windows 发布清单。

v0.1.13 完善联系人、放大阅读、引用和本机删除；
v0.1.14 新增加密本机收藏、搜索分类、独立图片保存、转发及多选操作。
v0.1.15 增加群图片，按实际群会话绑定资源，逐请求核验当前成员资格；
支持跨节点、离线历史、续传及去重，移除和退群后拒绝后续资源访问。
v0.1.16 增加私聊和群聊语音消息，支持录制、停止、试听、发送、取消、播放、收藏及转发，
使用 16kHz 单声道 16 位 PCM/WAV，时长 1–60 秒、不超过 2MiB，录音播放与实时通话互斥。
转发保留已有草稿，图片按新目标重新上传；本机删除不会撤回服务器或其他设备的消息。
发布与云端状态以对应标签的 Actions 部署结果为准，功能说明见 [消息菜单](CppChat/docs/message-menu.md)。

原项目仓库地址：https://github.com/secondtonone1/llfcchat

原项目作者B站主页：【C++ 全栈聊天项目(1)架构概述和登录界面】https://www.bilibili.com/video/BV1k2421K7ZB?vd_source=96af471cf08ee4437c4f053f59af4cb6

| 目录 | 内容 |
| --- | --- |
| `CppChat/` | Qt 客户端、Designer 界面、聊天气泡、HTTP/TCP 管理及图片样式资源 |
| `CppChatAndroid/` | Android QML 前端、平台桥接、账号与连接恢复；复用 `CppChat/` 公共业务源码 |
| `GateServer/` | HTTP 接入、注册登录、MySQL/Redis 访问、gRPC 客户端 |
| `GatewayGuard/` | 云端回环 HTTP 安全网关、Redis 限流与可信代理头校验 |
| `VerifyServer/` | Node.js 验证码和邮件服务 |
| `StatusServer/` | 聊天节点分配与 Token 校验 |
| `ChatServer/` | 第一个聊天节点入口及本机配置 |
| `ChatServer2/` | 第二个独立聊天节点入口及本机配置 |
| `ChatServerShared/` | 两个节点共用的 TCP、会话、好友/消息业务、DAO、连接池与生命周期实现 |
| `ResourceServer/` | 独立图片资源服务，负责认证、上传下载、文件元数据与断点续传 |
| `shared/media/` | 客户端与资源服务共同使用的无依赖 PCM/WAV 容器边界校验 |
| `database/` | MySQL 完整建库、建表脚本及客户端 SQLite 建表脚本，见 [数据库说明](database/README.md) |
| `cmake/` | 服务端公共构建、依赖、调试分组及测试插桩配置 |
| `tests/` | 单元、集成、回归、性能测试和人工验收工具 |

本机 Windows 操作入口为根目录的 `CppChat.cmd`，实现位于 `.runtime/cppchat.ps1`，命令行选项可运行 `CppChat.cmd help` 查看。构建时协议由 CMake 自动生成；手动更新源码目录中的 C++ Protobuf / gRPC 文件可选择菜单第 `9` 项，或运行 `CppChat.cmd generate-proto`，默认生成全部协议。指定单个协议可运行 `CppChat.cmd generate-proto -ProtocolTarget GateServer`，目标支持 `All`、`GateServer`、`StatusServer`、`ChatServerShared`。两个聊天节点共用 `ChatServerShared` 的协议，VerifyServer 在运行时加载协议，无需生成 C++ 文件。

协议生成复用已安装的 vcpkg Protobuf/gRPC 工具，沿用官方 [Protobuf C++ 生成方式](https://protobuf.dev/reference/cpp/cpp-generated/)和 [gRPC C++ 生成方式](https://grpc.io/docs/languages/cpp/basics/)，未引入新依赖。默认工具来自 `D:\Dev\vcpkg` 的 `x64-windows-static-md-release` 安装；可通过环境变量 `PROTOC`、`GRPC_CPP_PLUGIN`、`VCPKG_ROOT`、`VCPKG_TARGET_TRIPLET` 覆盖。本机版本见下文开发环境表；生成结果写入对应 `message.proto` 所在目录，CMake 构建使用的生成目录保持独立。

头像来源说明见 [AVATAR_SOURCES.md](CppChat/Resources/AVATAR_SOURCES.md)

## 配置

客户端工程入口为 `CppChat/CMakeLists.txt`，本机服务端 VS 工程位于 `build/server`。各组件目录中的 `config.ini`（VerifyServer 为 `config.json`）是本机配置修改入口，统一脚本会同步到运行目录。实际配置、凭据、运行数据、生成的 IDE 工程和测试报告均不上传 GitHub。客户端配置和 Qt 构建方式见 [客户端说明](CppChat/README.md)，图片服务环境变量见 [资源服务说明](ResourceServer/README.md)。

首次克隆后，需自行创建实际配置、安装依赖并生成构建目录。`CppChat.cmd` 是当前 Windows 开发环境的便捷入口，使用本机 VS、Qt、vcpkg 路径和已准备的 `.runtime` 环境；在其他机器上使用时需要调整工具路径并准备相应服务配置及运行依赖。跨平台构建使用 CMake 入口，Linux 隔离环境可参考 [CI 构建脚本](tests/ci/build-linux.sh)。

本机启动依赖 MySQL84 Windows 服务（`127.0.0.1:3306`）。服务停止时，启动器先尝试启动；权限不足时请求 Windows 管理员授权，仅提升数据库服务的启动命令。认证检查使用 `.runtime/mysql/client.ini`，按 [MySQL 官方客户端选项](https://dev.mysql.com/doc/refman/8.4/en/mysql-command-options.html)通过 `--defaults-file` 和 `--no-login-paths` 隔离全局配置及已保存的登录路径；连接失败显示原始 MySQL 错误，端口不匹配单独报错。停止项目仍保留 MySQL84 服务。

启动器使用 Windows 原生 [Get-NetTCPConnection](https://learn.microsoft.com/en-us/powershell/module/nettcpip/get-nettcpconnection)检查本机端口是否监听，避免通过连接后立即断开的探测方式产生 HTTP/TCP 读取错误日志；MySQL 和 Redis 仍分别执行真实认证检查。MySQL 弃用选项及连接池恢复方式见 [数据库连接恢复说明](CppChat/docs/server-database-recovery.md)。

验证码服务沿用现有 [ioredis 自动重连及状态事件](https://github.com/redis/ioredis#connection-events)：Redis 连接错误包含 ISO 时间戳，连接成功或恢复后记录 `Redis connection ready`，便于区分旧错误与当前状态；不在错误回调中关闭客户端，也不另建重连机制。

服务端总入口为根目录 `CMakeLists.txt`，各服务的目标定义分别放在 `GateServer`、`StatusServer`、`ChatServer`、`ChatServer2` 和 `ResourceServer` 的 `CMakeLists.txt` 中；`cmake/ServerDependencies.cmake` 管理公共依赖，`cmake/ServerTargets.cmake` 管理协议生成、源码调试映射和公共目标设置。总入口生成一个 `CppChatServers.slnx`，每个服务仍是独立工程。子目录由总入口组织，需要从仓库根目录配置构建。

VS 解决方案统一分为“服务端”“测试”“构建辅助”：五个业务工程放在“服务端”，测试工程放在“测试”，GoogleTest/GoogleMock 放在“测试/依赖”，CMake 自动目标放在“构建辅助”。分组使用 CMake 原生的 [FOLDER / CMAKE_FOLDER](https://cmake.org/cmake/help/latest/prop_tgt/FOLDER.html) 和 [PREDEFINED_TARGETS_FOLDER](https://cmake.org/cmake/help/latest/prop_gbl/PREDEFINED_TARGETS_FOLDER.html)，规则由公共服务配置及测试构建入口维护；只调整 IDE 展示，不修改第三方源码或构建依赖，重新生成仍保持一致。

双击 `CppChat.cmd` 后选择 `7`（Release）或 `8`（Debug）可单独编译一个服务，也可执行 `CppChat.cmd build -Target GateServer -Configuration Debug`。`-Target Server` 编译全部 C++ 服务，`-Target Client` 只编译 Qt 客户端，默认 `-Target All` 编译两者。在 VS 中右键对应工程选择“生成”也能单独编译。

## 回归测试

统一运行 `python tests/run.py --suite smoke|full|nightly|release`，准备环境和参数见 [测试说明](tests/README.md)。测试与业务源码分离，默认业务构建不加载测试依赖；MySQL、Redis、邮件和故障代理均由独立容器提供，不操作生产账号。

功能映射见 [用例目录](tests/cases.json)，Windows 桌面及多屏验收见 [人工清单](tests/manual/windows.md)。报告在被忽略的 `build/regression` 下，明确区分 PASS、FAIL、BLOCKED 和未实现。覆盖率及人工验收未完成时，不代表项目全部功能已通过。

## 手动建立工程

自行建立工程时：

- 两个聊天节点各使用自己的 `ChatServer.cpp`，分别编译 `ChatServerShared` 中的公共实现并加入其头文件搜索路径；不要把两个入口编进同一程序。
- GateServer 需将 `ChatServerShared/BusinessExecutor.cpp` 加入自己的工程，并加入公共头文件搜索路径。
- StatusServer 复用公共 `ServicePolicy.h`，不编入聊天节点入口。
- ResourceServer 源码位于根目录 `ResourceServer`，需加入该目录源码及公共 `BusinessExecutor.cpp`；Qt 客户端通过网络访问图片服务。
- Qt 客户端登记 `app/common/dialogs/network/services/widgets` 源码、Designer `.ui` 和 `rc.qrc`，其源码包含独立的 SQLite 工作线程。运行配置使用各组件目录中的本机配置。

还需要自己搭环境：C++ boost库、grpc、mysql、redis等，MSVC建议使用vcpkg让codex给你配

## 运行实例

客户端运行效果：

登录：

<img width="453" height="809" alt="image" src="https://github.com/user-attachments/assets/6c8c808b-9e42-4424-841b-6b80b6305349" />

重置密码：

<img width="452" height="810" alt="image" src="https://github.com/user-attachments/assets/a18cdc2e-c3fd-4535-915d-21ae6bc4543a" />

注册：

<img width="452" height="810" alt="image" src="https://github.com/user-attachments/assets/6bffa092-79f2-4638-ac66-40f33a70ae23" />

聊天：

<img width="1499" height="1049" alt="image" src="https://github.com/user-attachments/assets/fe7083f8-d45f-4a33-9314-198dce8a9d40" />

联系人：

<img width="1499" height="1049" alt="image" src="https://github.com/user-attachments/assets/99054d07-6706-4483-acd2-3b20f1f83a55" />




服务端运行效果：

GateServer：

<img width="1730" height="924" alt="image" src="https://github.com/user-attachments/assets/b42a2cdd-e698-4056-b094-592e7060bb76" />

VerifyServer：

<img width="1730" height="924" alt="image" src="https://github.com/user-attachments/assets/f2d77e6d-24b9-4f25-83ad-93f67f3d9fae" />

ChatServer：

<img width="1730" height="924" alt="image" src="https://github.com/user-attachments/assets/8101e060-52a4-4365-a2ee-a7a0a23c0a3a" />

ChatServer2：

<img width="1730" height="924" alt="image" src="https://github.com/user-attachments/assets/dbba2d10-09ed-4707-ac5c-818b5be7d2ac" />

StatusServer：

<img width="1730" height="924" alt="image" src="https://github.com/user-attachments/assets/fadcf977-a2a5-4f7a-815a-bb657f48697a" />




## 本机开发环境

以下版本于 2026-10-13 从本机工具和已安装的 vcpkg 包核对，供复现本次 Windows 构建参考；不是项目最低版本要求：

| 项目 | 版本 |
| --- | --- |
| 操作系统 | Windows 64-bit，服务端目标 x64 |
| C++ 标准 | C++17 |
| Qt | 6.12.0，MinGW 64-bit Kit |
| MinGW-w64 GCC | 13.1.0，Qt 配套工具链 |
| Visual Studio / MSVC Platform Toolset | VS2026 / v145 |
| Windows SDK | 10.0.26100.0 |
| CMake | 服务端 4.3.1-msvc1；客户端 3.30.5 |
| vcpkg | 2026-10-10 提交 `e456309491fd875487bf02b8a0dca76c1b1b00cc` |
| Boost | 1.92.0 |
| gRPC | 1.81.1 |
| Protocol Buffers | 6.33.4 |
| OpenSSL | 3.6.5，服务端依赖；Windows 客户端可使用 Qt Schannel TLS 插件 |
| MySQL Server | 8.4.11 |
| MySQL Connector/C++ | 9.7.0 |
| hiredis | 1.4.1 |
| Node.js | 24.21.0 |
| npm | 11.19.0 |
| Git for Windows | 2.56.0.windows.2 |

## 本地、GitHub 与云端源码同步

以 Git 提交作为三端源码版本标识：本地完成构建和验证后提交，通过普通 [Git push](https://git-scm.com/docs/git-push) 同步远程分支，再用 [git archive](https://git-scm.com/docs/git-archive) 导出同一提交到云端。不要上传整个工作目录；`.gitignore` 排除的连接配置、数据库密码、SMTP 授权码、密钥、聊天数据、日志、构建产物和依赖目录均不进入源码包。公开说明使用示例地址，真实连接信息只放在环境配置中。

提交前可使用 [Gitleaks](https://github.com/gitleaks/gitleaks) 扫描待发布源码。本轮采用官方 Windows x64 8.24.2（MIT），校验发布文件 SHA256，使用内置规则和 `--redact`；另核对真实服务器地址及本机配置中的凭据值。扫描结果仅作为检查证据，不能保证不存在所有敏感信息；第三方许可证和作者署名保留。扫描工具和报告放入被忽略的 `build/`，不增加业务依赖。

云端源码与运行目录分离：先备份源码、程序和必要的环境覆盖文件，再同步固定提交、构建 Linux 程序并检查服务。环境配置及数据库保持独立，不执行首次初始化部署脚本，不自动导入 SQL。发布记录保存提交 SHA、源码清单、程序 SHA256 和实际验证结果，失败时恢复上一版本；Windows 安装包有独立版本及发布流程，源码同步不会自动替换已发布安装包。


