# CppChat Android

Qt Quick / QML 安卓客户端，当前版本 0.1.22，ARM64 / Android 9+。
与桌面及服务端同仓库维护，复用 `../CppChat` 中的协议、业务、持久化、资源与媒体模块；界面与平台生命周期在本目录实现。

## 目录

| 目录 | 职责 |
| --- | --- |
| `app/` | Android QML 程序入口 |
| `qml/` | 页面、控件及 AppBridge / 账号 / 恢复 / 媒体门面 |
| `android/` | 清单、脱敏配置示例及随包许可证 |
| `scripts/` | CMake Android 构建、签名检查与 OpenSSL 构建 |
| `tests/` | 门面、恢复、媒体、QML 与 APK 检查源码 |
| `third_party/qtkeychain/` | 固定版本的系统凭据存储库及许可证 |
| `docs/` | 构建、功能与验证边界说明 |
| `build/` | 本机产物、依赖、密钥、报告及归档，不入 Git |

## 构建与配置

Qt Creator 打开本目录 `CMakeLists.txt`。需要 Qt 6.9+（已验证 6.12.0）、SDK 36、NDK r27c、JDK 21+、Ninja 和支持 ARM64 的 OpenSSL 3。
CMake 默认使用同仓库 `../CppChat`；前端单独存放时传入 `-DCPPCHAT_BACKEND_ROOT=<配套客户端源码目录>`。
将 `android/config.example.ini` 复制为 `android/config.ini` 并配置自己的服务；真实配置、签名和机器路径禁止提交。
完整命令、依赖版本、许可、APK 位置和联调范围见 [Android 构建说明](docs/ANDROID_BUILD.md)。

`Android APK` Actions 对安卓分支每次 push 自动构建、签名并检查 ARM64 APK，产物在运行页面的 Artifacts 下载。
`android-v<版本>` 标签另外发布 GitHub Release；普通提交不改线上更新清单。
0.1.21 起使用重新配置的固定签名，旧 0.1.20 无法沿用原签名覆盖升级，需要先卸载重装；卸载会删除本机应用数据，应先导出需要保留的内容。

已接入账号记忆与自动登录、桌面/手机双端在线、断线恢复、本地历史、更新检测、群管理，以及图片、文件、语音消息和语音/视频通话。
当前证据区分主机构建/QML、隔离服务与真实 TLS 协议检查；Android 真机 Keystore、系统权限、输入法与音视频体验仍需设备验收。
厂商推送尚未开通，进程被系统回收后不能保证即时通知；桌面所有功能尚未全部移植。

源码遵循仓库 MIT 许可；Qt、QtKeychain、Opus、OpenSSL 等保留各自许可，见随包 `android/assets/licenses/` 和依赖说明。
