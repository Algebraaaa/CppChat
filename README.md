# CppChat

B站恋恋风辰大佬的C++全栈即时通讯项目，我在辰哥的基础上做了改良，给服务端和客户端加了日志类，做了一些美化，以及加了很多个人的注释

原项目仓库地址：https://github.com/secondtonone1/llfcchat

原项目作者B站主页：【C++ 全栈聊天项目(1)架构概述和登录界面】https://www.bilibili.com/video/BV1k2421K7ZB?vd_source=96af471cf08ee4437c4f053f59af4cb6

| 目录 | 内容 |
| --- | --- |
| `CppChat/` | Qt 客户端、Designer 界面、聊天气泡、HTTP/TCP 管理及图片样式资源 |
| `GateServer/` | HTTP 接入、注册登录、MySQL/Redis 访问、gRPC 客户端 |
| `VerifyServer/` | Node.js 验证码和邮件服务 |
| `StatusServer/` | 聊天节点分配与 Token 校验 |
| `ChatServer/` | TCP 会话、消息组帧、发送队列和登录处理 |
| `ChatServer2/` | 第二个聊天服务实例源码，用于多节点通信与分配场景 |

头像来源说明见 [AVATAR_SOURCES.md](CppChat/Resources/AVATAR_SOURCES.md)

## 配置

此仓库只有源代码，没有各种工程文件，需要自行配置工程、安装依赖，并根据相应工具版本重新编译，配置文件里面买的CHANGE_ME需要更换你本机的密码等参数，示例中的密码、授权码和账号均为占位值。

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

以下版本为本项目当前使用的本机开发、构建与联调环境，仅供复现时参考：

| 项目 | 版本 |
| --- | --- |
| 操作系统 | Windows 10 Pro 25H2（内部版本 26200，按本机注册表显示） |
| C++ 标准 | C++17 |
| Qt | 6.5.3（MinGW 64-bit） |
| MinGW-w64 GCC | 11.2.0（Qt 配套工具链） |
| MSVC Platform Toolset | v143 |
| Windows SDK | 10.0 |
| CMake | 4.0.3 |
| vcpkg | 2026-07-13（bf04c909169fdbb30821c02c6eb01f1cd1295d05） |
| Boost | 1.91.0 |
| gRPC | 1.81.1 |
| Protocol Buffers | 6.33.4#2 |
| OpenSSL | 3.6.3 |
| MySQL Server | 8.0.42 |
| MySQL Connector/C++ | 9.7.0 |
| hiredis | 1.3.0 |
| Node.js | 24.12.0 |
| npm | 11.6.2 |
| Git for Windows | 2.50.1.windows.1 |


