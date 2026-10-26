# CppChat

C++ / Qt 即时通讯项目，包含 Windows 客户端、Android 客户端和 Linux 服务端。
支持私聊、群聊、图片与文件、语音消息、语音及视频通话。

**[下载与版本说明 → GitHub Releases](https://github.com/Algebraaaa/CppChat/releases)**

## 源码与开发

| 目录 | 内容 |
| --- | --- |
| `CppChat/` | Windows / Qt Widgets 客户端与共享客户端业务 |
| `CppChatAndroid/` | Android / Qt Quick 客户端 |
| `GateServer/`、`StatusServer/`、`ChatServer*`、`ResourceServer/` | C++ 服务端 |
| `VerifyServer/` | Node.js 验证码与邮件服务 |
| `database/`、`tests/` | 数据库脚本与隔离测试 |

- [分支、合并与发布流程](CONTRIBUTING.md)
- [Windows 客户端构建](CppChat/README.md) · [Android 构建](CppChatAndroid/docs/ANDROID_BUILD.md)
- [服务端架构](CppChat/docs/backend-architecture.md) · [数据库配置](database/README.md)
- [测试与验收](tests/README.md)

服务端从根目录 `CMakeLists.txt` 构建，两个客户端各有自己的 CMake 入口。
实际连接配置、密钥、聊天数据和构建产物不提交到仓库；配置示例随对应组件提供。

## 来源与许可

基于 [恋恋风辰 / llfcchat](https://github.com/secondtonone1/llfcchat) 改进，
原作者教程见 [B 站项目介绍](https://www.bilibili.com/video/BV1k2421K7ZB)。
本仓库代码采用 [MIT 许可](LICENSE)；第三方代码、字体和图形资源保留各自许可与署名，
头像来源见 [资源说明](CppChat/Resources/AVATAR_SOURCES.md)。
