# CppChat

B站恋恋风辰大佬的C++全栈即时通讯项目，我在辰哥的基础上做了改良，给服务端和客户端加了日志类，做了一些美化，以及加了很多个人的注释

| 目录 | 内容 |
| --- | --- |
| `CppChat/` | Qt 客户端、Designer 界面、聊天气泡、HTTP/TCP 管理及图片样式资源 |
| `GateServer/` | HTTP 接入、注册登录、MySQL/Redis 访问、gRPC 客户端 |
| `VerifyServer/` | Node.js 验证码和邮件服务 |
| `StatusServer/` | 聊天节点分配与 Token 校验 |
| `ChatServer/` | TCP 会话、消息组帧、发送队列和登录处理 |


## 文件范围

此仓库只有源代码，没有各种工程文件，需要自行配置工程、安装依赖，并根据相应工具版本重新编译

头像来源说明见 [AVATAR_SOURCES.md](CppChat/Resources/AVATAR_SOURCES.md)

## 配置示例

各 C++ 模块的 `config.example.ini` 重命名为为 `config.ini`，验证码服务的 `config.example.json` 重命名为 `config.json`，然后填写自己的运行参数。示例中的密码、授权码和账号均为占位值。

## 当前进度

正在完善
