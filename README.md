# CppChat

基于 Qt Widgets、C++ 和 Node.js 的即时通信学习项目。本仓库保存客户端与四个服务端的源码、界面资源、协议定义及数据库初始化脚本。

| 目录 | 内容 |
| --- | --- |
| `CppChat/` | Qt 客户端、Designer 界面、聊天气泡、HTTP/TCP 管理及图片样式资源 |
| `GateServer/` | HTTP 接入、注册登录、MySQL/Redis 访问、gRPC 客户端 |
| `VerifyServer/` | Node.js 验证码和邮件服务 |
| `StatusServer/` | 聊天节点分配与 Token 校验 |
| `ChatServer/` | TCP 会话、消息组帧、发送队列和登录处理 |

技术包括 Qt Widgets、Boost.Asio/Beast、gRPC/Protobuf、MySQL、Redis、OpenSSL、JsonCpp，以及 Node.js 的 `@grpc/grpc-js`、`@grpc/proto-loader`、`ioredis`、`nodemailer`。各服务保留自己的 `.proto` 文件和现有生成源码。

## 文件范围

按源码归档方式整理，不包含 Qt / Visual Studio / CMake 工程文件、Node.js 依赖清单、构建产物、第三方安装目录、运行日志或本机真实配置。使用者需要自行配置工程、安装依赖，并根据相应工具版本重新生成必要的代码。本仓库不是开箱即用的构建包。

Qt 的 `.ui` 是界面定义，`.qrc` 是资源清单，均作为源码与资源保留。头像来源说明见 [AVATAR_SOURCES.md](CppChat/Resources/AVATAR_SOURCES.md)。

## 配置示例

各 C++ 模块的 `config.example.ini` 可复制为 `config.ini`，验证码服务的 `config.example.json` 可复制为 `config.json`，然后填写自己的运行参数。示例中的地址统一为回环地址，密码、授权码和账号均为占位值，不能直接用于真实认证。

客户端通常从可执行文件所在目录加载配置；各服务应按自身配置读取方式放置配置文件。真实配置已列入 `.gitignore`，不要强制提交凭据或运行日志。

## 当前进度

已有账号相关流程、TCP 登录认证和聊天界面的实现；客户端仍使用聊天主页测试入口，聊天发送按钮目前完成本地气泡展示。双客户端消息转发、聊天记录持久化与断线恢复尚待完善。

此次发布核对了源码复制、配置脱敏与 Qt 资源引用，没有执行构建、端到端联调或性能测试。
