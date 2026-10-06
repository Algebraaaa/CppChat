# CppChat

C++ / Qt Widgets 聊天客户端。聊天业务通过 ChatServer 处理，图片原始字节通过
ResourceServer 传输，本地消息和草稿保存在按服务端及账号隔离的 SQLite 数据库。

## 开发与维护入口

- 在 Qt Creator 中打开 `CppChat.pro`，选择对应 Qt Kit，运行 qmake 后构建。
- `client.pri` 登记客户端源码、UI 和资源；`.ui` 管布局，QSS 管外观，C++ 管行为。
- [项目目录与约定](docs/project-guide.md)
- [界面功能](docs/ui-features.md)与[本地聊天记录](docs/local-chat-history.md)
- [性能优化实现和验证](docs/performance-improvements.md)
- [架构加固阶段与验证](docs/architecture-hardening-20261006.md)和[客户端流程服务](services/README.md)
- [服务端架构](docs/backend-architecture.md)与[资源服务构建配置](resource_server/README.md)

`config.ini`、真实账号数据库、图片缓存、日志和编译产物属于本机文件，不应发布。

## 本机验证环境

2026-10-04 核对的实际工具版本；这是本次验证环境，不是项目最低版本要求。

| 工具或依赖 | 版本 / 配置 |
| --- | --- |
| Qt | 6.5.3，MinGW 64 位 Kit |
| 客户端编译器 | MinGW GCC 11.2.0，C++17 |
| GNU Make | 4.2.1 |
| ResourceServer 构建工具 | Visual Studio 2022，x64 Debug |
| CMake | 4.0.3 |
| 验证脚本 Python | 3.12.12 |
| 服务端依赖方式 | vcpkg，x64-windows |
| Boost | 1.91.0 |
| OpenSSL | 3.6.3 |
| 客户端日志 | vendored spdlog 1.15.3，header-only |

2026-10-04 的客户端、资源服务与 Qt 回归测试构建曾放在 `build/performance-validation/`，
该临时目录于 2026-10-06 清理时移入 Windows 回收站。
完整验证步骤、结果和边界见性能文档；隔离协议测试使用 RESP/TTL 模拟服务，
不等同真实 Redis、MySQL 和多客户端联调。

## 2026-10-06 源码清理

按用户要求删除客户端的 `scripts/` 和 `tests/`，并移除 `CppChat.pro` 中的对应登记。
已有测试结果与环境表作为历史记录保留，相关脚本不再包含在当前源码中。
随后也清理共享服务端的测试/架构检查脚本、临时验证构建和专项测试报告；正常开发构建、业务源码、启动/协议生成脚本及真实聊天数据保留。
内置表情、业务代码和 `third_party/spdlog` 保留；正常客户端构建不依赖这些测试或脚本。

## 2026-10-06 本地聊天数据位置

开发运行时从 EXE 目录向上找到 `CppChat.pro`，聊天数据放在本项目根目录下：
`chat_data/accounts/<服务器哈希>/<UID>/sql/db.sqlite`，图片缓存仍在账号目录下的 `attachments/`。
不使用项目外独立目录，不写入 C 盘，不随 build 子目录变化。
发布目录没有项目文件时使用 EXE 同级的 `chat_data/`；目标位于 C 盘或不可写时明确报错。
首次登录会兼容迁移旧 `chat.db` 与附件；已有目标数据库不会被覆盖，旧库保留。

本机现有账号随后已按用户要求直接迁移并逐表核验：22 条消息、12 个会话及同步状态完整保留。
旧 Debug 运行目录和旧 AppData 副本的数据库及 WAL/SHM 已删除，常用 Debug EXE 已更新为使用新路径的版本。

同日递归检查并将性能验证构建、日志专项构建、Python 缓存、Qt Creator 索引缓存
和旧日志移入 Windows 回收站，共 1,868 个文件、约 963 MiB。
正常客户端及资源服务构建、私人配置、聊天数据库和资源数据保留；
客户端和资源服务各保留一份最近运行日志。回收站未清空，文件仍可恢复。
