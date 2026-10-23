# CppChat Android 构建与联调

## 当前交付

综合安装包为 `build/release/CppChat-0.1.20-android-arm64.apk`（versionCode 120），
保留同包名、同测试签名和既有应用数据。包含下述基础适配、会话恢复和本轮媒体功能。
配套服务和安卓更新清单已上线；最终 APK SHA256 为 `8d8ebbca328aecb933fe5f052141195265b06f40bf57c726242e6651ff9427f3`。
0.1.19 的测试与交付记录仅为基础版本历史，不能代替本轮验证。

### 0.1.20 媒体适配

聊天页“+”可发送图片、文件或录制语音；录音完成后确认发送或取消，语音最长 60 秒。
消息气泡可播放/停止语音、显示文件传输进度、下载及通过系统选择器另存文件。
私聊标题菜单提供语音/视频通话，来电面板可接听、拒接、挂断，通话中可静音、关闭摄像头及切换前后镜头。
同账号电脑与手机同时在线时，呼叫优先送到手机；未登录手机则送到电脑。一次通话绑定实际双方设备与会话，
同账号另一台设备不能抢接或挂断，媒体也不会发给未接听设备。
视频沿用现有桌面协议的 JPEG 受限帧（最高 320×240、8 帧/秒），语音通话沿用 48kHz/20ms Opus。
本轮没有更换为 WebRTC，也不把这条现有低码率链路描述为高清视频。

`MobileMedia` 负责移动端权限、设备生命周期和预览桥接，直接复用
`VoiceCallService`、`VoiceAudio`、`VideoMedia`、`VoiceMessageAudio`；QML 不实现媒体协议。
离开聊天停止语音录放；进入后台、断线或退出时停止采集和通话，迟到的权限/下载回调不能自动重新开启设备。
权限被拒绝时提供系统设置提示。来电面板只覆盖当前运行中的应用；进程被回收后的来电推送仍需下述推送通道。

文件沿用 `AttachmentPreparer → MessageSender → ResourceMgr → ResourceServer`，共享桌面端增加文件发送按钮和下载/保存气泡。
电脑互传需要使用这份配套源码编译的桌面客户端；旧的已发布 EXE 收到文件时会显示升级提示。
单个文件连同名称信息最多 20 MiB；使用 Qt Core 的标准 CBOR map `{v:1,name:text,data:bytes}` 保留名称和原始字节，
拒绝路径、控制字符、损坏/未来版本和超限内容。网络仍使用原有 32 KiB 分块、SHA256、断点和待确认消息重试；
服务端以 `file:` 范围隔离图片/语音，并检查原有好友/群成员权限。未协商 `file_messages` 的旧客户端收到升级提示。
收到文件不会自动执行或向公共目录写入；仅明确选择保存目标后导出，应用内副本继续使用现有账号附件存储。

复用与适配依据：

- [Qt v6.12.0 摄像头示例](https://github.com/qt/qtmultimedia/blob/v6.12.0/examples/multimedia/camera/camera.cpp)（BSD-3-Clause）和[权限接口](https://doc.qt.io/qt-6/permissions.html)：参考按操作申请权限，未复制示例 UI；实际使用 `QPermissions` 和现有 Qt Multimedia。
- [Qt v6.12.0 Android 系统文件选择器](https://github.com/qt/qtbase/blob/v6.12.0/src/plugins/platforms/android/qandroidplatformfiledialoghelper.cpp)：直接使用已部署的 Qt 实现及 ACTION_CREATE_DOCUMENT/授权 URI，不引入全盘存储权限或新 Android 文件库；未复制其 LGPL 源码。
- [Qt CBOR](https://doc.qt.io/qt-6/qcborvalue.html)：使用项目已有 Qt Core 编解码，避免自制二进制容器或额外压缩库。
- [Opus 1.5.2](https://github.com/xiph/opus/releases/tag/v1.5.2)：直接复用桌面已固定 SHA256 的 `cmake/VoiceAudio.cmake`，为 ARM64 编译，保留 BSD 许可于 `android/assets/licenses/Opus-1.5.2.txt`。核对上游发布、测试与[公开安全公告](https://github.com/xiph/opus/security/advisories)；公告页当时无已发布条目，不等于完整安全审计。真实编解码、坏帧/序号及传输由项目既有测试验证。

本轮证据：本工程和共享后端的 `build/regression/mobile-media-20261023/`。
主机媒体 3 个业务用例通过；文件/语音/视频/多设备隔离联调 32 项通过，包含六个多设备通话场景、真实 Qt 门面 TLS 文件往返及原生 Opus/JPEG 传输探测；
36 个 QML 场景无警告，另检查截图。ARM64 APK 的签名、ZIP 与全部 102 个 ELF 的 16 KiB 对齐、媒体运行库及权限审计通过。
Windows 桌面及 Linux 三服务也完成构建，失败尝试的日志仍保留。
配套云端已应用 `20261023-dc7d101c8f31` 媒体补丁：公网 TLS 的 36 项协议检查通过，
包括双向文件/语音消息校验、安卓呼叫、桌面到安卓的视频和双端并存；临时账号/资源已回收。
数据库/原资源/配置及程序哈希复核后取消回退计时，服务端回退备份保留于部署环境，不纳入公开源码。
这类合成媒体的公网验证不等于真人音视频质量验证。
当前无连接手机；Android 麦克风/扬声器、权限拒绝/恢复、镜头方向、系统另存 URI、软键盘和电脑手机实机互通仍待实际安装验收。
Opus 1.5.2 由 CMake 按固定 SHA256 获取并针对目标 ABI 构建。

Android 前端位于仓库 `CppChatAndroid/`，CMake 默认复用同仓库的 `../CppChat/`。
单独保存前端时，通过 `CPPCHAT_BACKEND_ROOT` 指向配套桌面源码目录；公共协议、ChatStore、
MessageSender、资源管理、更新与媒体服务只有一份实现，不维护旧 `common/`、`network/` 副本。
主入口为 `CppChatAndroid/CMakeLists.txt`，在 Qt Creator 中直接打开它；当前维护并验证 CMake 入口。
本机旧桌面副本、qmake 兼容入口和旧缓存仅作为不入库的迁移归档保留。

- 安装包：`build/release/CppChat-0.1.20-android-arm64.apk`。
- ARM64，Android 9 / API 28 起；target/compile SDK 36；版本 0.1.20 / versionCode 120。
- 原生代码使用 Release 构建，APK 使用本轮本地测试证书签名，可侧载安装；不是商店正式签名。
- 测试密钥保留在 `build/signing/cppchat-android-test.jks`，供后续覆盖升级。不要上传密钥。
- 公共源码只提供 `android/config.example.ini` 的保留域名占位。将其复制为被忽略的 `android/config.ini` 并填写部署端点；也可设置 `CPPCHAT_ANDROID_CONFIG_FILE` / 构建脚本 `-ConfigFile` 指向外部私有配置。聊天节点仍由登录响应分配，连接校验证书。
- APK 不包含联调账号、密码、本机聊天库或日志。普通页面不显示服务器地址，也没有服务器编辑入口。配置只由构建及部署维护。

将 APK 传到 ARM64 手机后安装；沿用上一包的应用 ID 和测试签名，覆盖升级保留应用数据。
新服务端支持同账号一台桌面端与一台 Android 同时在线，同平台再次登录替换该平台旧连接。
此能力依赖本轮配套服务端，不是安装旧服务器上的新 APK 就会自动启用。

## 分层与适配范围

`qml/pages/` 与 `qml/components/` 只处理移动端展示、导航和输入。
`AppBridge` 是界面门面：`appbridge.cpp` 处理配置与认证，`appbridge_chat.cpp` 转接聊天服务和展示数据，
`appbridge_features.cpp` 将账号、更新、本地记录及群管理操作转成既有服务调用。
`MobileAccount` 单独负责登录偏好与 QtKeychain 的异步安全凭据生命周期，不让 QML 访问配置文件或密文。
`ChatSyncService` 负责账号恢复、历史分页与持久化合并；`MessageSender` 负责先保存、再上传/发送、确认和重试。
这两项直接复用 Windows 端已有实现，避免 Android 再维护一套消息协议。
`HttpMgr`、`TcpMgr`、`ResourceMgr`、`ChatStore`、`InputValidator`、头像缓存也直接复用。
为兼容现有 `UserMgr` 的 QLabel 接口仍链接 Qt Widgets，界面实际使用 QML，不创建桌面窗口。

本轮包括：

- 修正认证字段为现有协议的 `passwd`，读取顶层 `host/port/token/uid`，TCP 连通后继续执行聊天登录。
- 错误码与 JSON 格式检查、请求忙碌状态；注册/重置共用表单并复用输入校验；重置包含用户名。
- 安全区由 Qt 6.9+ `ApplicationWindow` 管理；软键盘使用 Android `adjustResize`，表单可滚动并将焦点输入框滚入视口。
- Android 返回键逐层返回；主动退出、同平台被替换清空导航栈和账号内存状态，普通断线保留页面并自动恢复。
- 会话筛选、联系人发起私聊、用户搜索、发送和接受好友申请、个人资料编辑与退出登录。
- 移除账号页面的服务地址；增加记住密码、自动登录与清除凭据。仅在完整登录并打开账号库成功后保存密码；主动退出关闭自动登录并保留记忆选项。
- 增加文字草稿、置顶、分页历史搜索、单会话清空本地记录，以及群创建、查看成员、改名、邀请、移除、转让、退出/解散界面。
- 启动及前台每六小时检查独立安卓版本清单；支持手动检查、下载进度、大小/SHA-256 校验及系统安装器。
- 文字/图片收发、服务端确认、失败重试、已发送与待确认消息合并、更多本地历史。
- 输入内容等到本地保存成功才清空；图片预处理在原有工作线程中执行，系统文件选择器使用授权 URI。
- 头像复用已有缓存；聊天正文按纯文本呈现；长文本换行；按钮使用适合触摸的尺寸。
- 配置、日志、SQLite 和附件位于应用私有目录，通过 `RuntimePaths` 统一传给所有后端模块。
- 不申请通用存储权限。录音/通话按用户操作申请麦克风权限，视频另申请摄像头权限；相机和麦克风硬件为可选特性。APK 侧载升级使用 REQUEST_INSTALL_PACKAGES，用户在系统中授权后确认安装。
- 账号密码由 QtKeychain 的 Android Keystore 后端保护，禁止不安全回退。聊天 SQLite/附件仍采用应用私有目录方案；这不等于 Windows 的数据库/附件加密能力。

基础版本曾将语音/视频通话、语音录制留待后续；0.1.20 已接入，范围及验证见下节。后台推送、桌面端笔记/提醒/收藏等仍需后续适配。
群图片继续走已更新的共享 MessageSender/ResourceMgr；不据此声称桌面所有功能已完成。
真机输入法、刘海/手势导航栏、系统图片选择器及 Windows GUI 与手机互发仍需实际安装验收。

## 已验证的工具链与配置

| 工具 | 版本 |
| --- | --- |
| Qt Android / host tools | 6.12.0，ARM64 Android 与 MinGW 主机 Kit |
| Android SDK | platform / build-tools 36 / 36.0.0 |
| NDK | r27c，27.2.12479018 |
| JDK | 24.0.1；Qt 6.12 要求 21+ |
| Gradle / AGP | 9.5.1 / 9.2.1，使用 Qt 模板 |
| OpenSSL | 3.5.9 LTS，源码构建至 `build/openssl-arm64` |

工具目录通过参数或本机环境变量指定，不把个人安装路径写入公开构建配置。
Qt Creator 直接打开 CMakeLists.txt；本机的 `CMakeUserPresets.json` 可保存 Kit、依赖和部署路径，该文件禁止入库。

## 重新生成安装包

### GitHub Actions 自动交付

`.github/workflows/android.yml` 的 `Android APK` 在 `android`、`android/**`、
`codex/android`、`codex/android/**` 的每次 push、目标为 `android` 的 PR 及手动执行时构建。
不设置路径过滤，不因新提交取消旧构建，因此共享后端的修改也会触发 APK 检查。
每个 push 构建其触发提交；一次 push 包含多条提交时构建该次 push 的最终版本。

运行页面 Artifacts 中的 `android-arm64-<commit>-<attempt>` 包含 APK、SHA256、更新 JSON
与 `.build.json` 来源记录，保留 30 天；`android-validation-<run>-<attempt>` 保留构建、签名、
16 KiB ZIP/ELF、权限及 TLS/媒体库检查证据 14 天，失败同样上传日志。
这是构建和包检查，不能代替安装启动、Android Keystore 或真实音视频验收。

复用本项目 `build_android.ps1`、`build_android_openssl.py` 与 `audit_android_apk.py`；
仅给既有脚本补 Linux 工具路径和外部签名接口，不维护第二套 APK 构建命令。
GitHub 托管 Ubuntu 24.04 使用 Qt 6.12.0 Linux 主机工具及 Android ARM64、
JDK 21、SDK/build-tools 36/36.0.0、NDK 27.2.12479018。
OpenSSL 仍为固定官方 SHA256 的 3.5.9，缓存键包含 NDK、系统和构建脚本哈希；
只缓存 Qt/OpenSSL 公共依赖，不缓存私有配置或密钥。

仓库 Actions Secrets：

| 名称 | 用途 |
| --- | --- |
| `CPPCHAT_ANDROID_CONFIG` | 构建时的 HTTPS 网关、聊天 TLS 和资源 TLS 配置，禁止含服务端口令 |
| `CPPCHAT_ANDROID_KEYSTORE_BASE64` | 固定 Android JKS/PKCS12 签名身份的 Base64，不能提交到仓库 |
| `CPPCHAT_ANDROID_KEY_ALIAS` | 同一身份中的签名别名 |
| `CPPCHAT_ANDROID_STORE_PASSWORD` | keystore 密码，通过环境变量传给签名程序 |
| `CPPCHAT_ANDROID_KEY_PASSWORD` | 私钥密码，通过环境变量传给签名程序 |

PR 不获得上述配置或签名身份，使用保留域名示例配置及一次性测试签名；其 APK 仅供构建验证。
普通 push 缺少 Secrets 时同样生成明确标记的测试构建；发行标签缺少固定身份或部署配置时直接失败。
签名文件和私有配置在 runner 收尾删除，不进入 Artifacts；已编入 APK 的客户端端点本来就能被客户端用户读取，不能用于保存服务端秘密。

正式生成 GitHub 下载包时，先提升 `CMakeLists.txt` 的三段版本及 versionCode，
再推送指向该提交的 `android-v<版本>` 标签。标签必须与项目版本一致；
APK 和更新清单使用标准发行文件名，成功后创建独立 Android GitHub Release，
不会把安卓包标成 Windows 的 latest。已有发行不覆盖不同内容。
普通 push 的文件名带 `ci-<commit>`，仅交付 Actions 产物；服务器更新清单仍由已有
`deploy-updates.py --platform android` 事务式发布控制，避免每次源码提交替换用户正在下载的包。

0.1.21 使用用户授权重新生成的固定签名。旧 0.1.20 的私钥未找到，Android 不允许不同
证书覆盖安装同一应用；首次改用新版须卸载重装，先导出需要保留的本机数据。
之后沿用同一签名并提高版本/versionCode。签名与受保护的密码备份保留在构建维护机的
忽略目录中，不能随着测试清理删除；GitHub Secrets 是 runner 的签名来源。
这项 Android APK 签名与 Windows Authenticode 证书是独立机制。

方案依据与依赖审查：使用 [Qt 官方 CMake APK 目标](https://doc.qt.io/qt-6/android-building-projects-from-commandline.html)
和 [Qt 6.12 前置版本](https://doc.qt.io/qt-6/android-configure-dev-environment.html)，
[install-qt-action v4.4.1](https://github.com/jurplel/install-qt-action/tree/a9c63c7c123f3069cff414e7e482d95dfa9d8125)
沿用仓库已有 MIT 工具及固定 aqt 提交，上游有安装矩阵与 issue 跟踪；
[upload-artifact v4](https://github.com/actions/upload-artifact) 使用 GitHub 官方 MIT 实现及不可变运行产物。
Actions 全部固定提交，最小 `contents: read`，仅标签发布 job 可写 Release；
没有引入新的客户端运行库，也不授予 PR 云端部署权限。
OpenSSL Linux 适配依据固定版本的 [NOTES-ANDROID](https://github.com/openssl/openssl/blob/openssl-3.5.9/NOTES-ANDROID.md)，
避免 Windows/MSYS2 专用路径进入 Linux。现有库许可不变；这些检查不是全量安全审计。

从仓库根目录进入 `CppChatAndroid` 后，复制 `android/config.example.ini` 为 `android/config.ini`，
填写自己的 HTTPS 网关及资源服务地址。示例域名不可用于登录；真实地址只写私有配置，普通 UI 没有编辑入口。
下例环境变量分别指向本机工具根目录；默认公共后端为同仓库的 `../CppChat`。

```powershell
./scripts/build_android.ps1 `
  -BackendRoot (Resolve-Path ../CppChat) `
  -QtAndroidRoot $env:QT_ANDROID_ROOT -QtHostRoot $env:QT_HOST_PATH `
  -SdkRoot $env:ANDROID_SDK_ROOT -NdkRoot $env:ANDROID_NDK_ROOT `
  -OpenSslRoot "$PWD/build/openssl-arm64" -JavaRoot $env:JAVA_HOME `
  -Ninja $env:NINJA -CMake $env:CMAKE `
  -GradleCache $env:GRADLE_USER_HOME
```

脚本要求实际私有配置存在；直接 CMake 构建若未提供私有配置，会使用无真实端点的示例，便于源码编译验证。
配置作为独立 Qt 资源加入共享移动门面对象，所有入口仍读取 `:/android/config.ini`。

脚本执行构建、测试签名、签名校验、ZIP 16 KiB 对齐校验、清单检查、ELF/TLS 包内容检查，
报告放在 `build/regression/`，失败时保留日志。签名密码 `android` 仅用于公开性质的本地测试证书。
正式发行须接入自己的签名流程。脚本不自动推送、部署服务器或安装到手机。

需要重新构建 TLS 运行库时，先取得官方 `openssl-3.5.9.tar.gz`，再执行：

```powershell
python -B scripts/build_android_openssl.py `
  --archive build/dependencies/openssl-3.5.9.tar.gz `
  --workdir build/dependencies --ndk $env:ANDROID_NDK_ROOT `
  --msys $env:MSYS_ROOT --output build/openssl-arm64
```

脚本固定校验官方 SHA256：
`603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a`。
使用上游 Android 构建配置，启用共享库和 16 KiB LOAD 对齐；关闭测试、工具程序、外部模块和汇编。
适配 Qt 所需的 `_3.so` 文件名、标准 `OPENSSL_3.*` 导出版本；Windows 长链接命令通过响应文件执行，保持上游链接参数。
OpenSSL 的 Apache 2.0、项目 MIT 和 spdlog MIT 声明位于 `android/assets/licenses/`。
思源黑体及原有资源声明随 Windows 工程的 `rc.qrc` 一并打包。

## 来源、选择与兼容性依据

- [Qt 官方聊天示例，固定 v6.8.3](https://github.com/qt/qtdeclarative/blob/v6.8.3/examples/quickcontrols/chattutorial/chapter3/ConversationPage.qml)：参考 Page/StackView、消息列表与底部编辑器的组织方式；示例标注 BSD-3-Clause。未复制整套示例或引入新界面框架。
- [Qt ApplicationWindow](https://doc.qt.io/qt-6/qml-qtquick-controls-applicationwindow.html)：使用 6.9 起提供的安全区处理，实际交付锁定本机 6.12.0 Kit。
- [Qt 样式选择](https://doc.qt.io/qt-6/qtquickcontrols-styles.html)：显式导入 Material。系统文件选择器采用 Qt Quick Dialogs，其官方回退控件的传递主题依赖由部署工具保留，没有手工删改 Qt 插件。
- [Qt Android 清单模板 v6.12.0](https://github.com/qt/qtbase/blob/v6.12.0/src/android/templates/AndroidManifest.xml)：采用正确的 `bindings.QtApplication/QtActivity`、库名替换标记与 FileProvider。
- [Qt 6.12 Android 前置条件](https://doc.qt.io/qt-6/android-configure-dev-environment.html)：对齐 SDK 36、NDK r27c 和 JDK 21+。
- [KDAB android_openssl 固定提交](https://github.com/KDAB/android_openssl/tree/b71f1470962019bd89534a2919f5925f93bc5779)：仅参考 Qt 库名与导出适配方式；仓库现有 3.1.8 预编译库未采用。
- [OpenSSL 官方版本与校验值](https://openssl-library.org/source/)及[3.5 发布记录](https://mirror.openssl-library.org/news/openssl-3.5-notes/)：采用受支持的 3.5.9 LTS 安全补丁版本，从源码构建 ARM64。
- 本轮核对 Qt 已公布的 [QNetworkAccessManager](https://www.qt.io/blog/security-advisory-cve-2026-96432)、[JSON](https://www.qt.io/blog/security-advisory-cve-2026-101201)、[SVG](https://www.qt.io/blog/security-advisory-cve-2026-8168) 和 [VectorImage](https://www.qt.io/blog/security-advisory-cve-2026-105923) 公告；当前 6.12.0 不在这些公告列出的受影响范围。该核对不等于全量安全审计。

## 本地数据、多端与后续发布

Android 数据根目录由 `QStandardPaths::AppDataLocation` 提供，通常为
`/data/user/0/org.algebra.cppchat/files`。账号库进一步按服务器身份摘要和 UID 隔离，
实际数据库位于该账号目录的 `sql/db.sqlite`，WAL/附件随账号保存。
“我 → 账号、数据与更新”显示当前账号实际路径及占用；电脑与手机各有自己的库。
退出保留记录；卸载或系统“清除应用数据”删除本机记录。单会话清空保留未确认消息和隐藏边界，
不删除云端/另一设备内容，不把旧消息重新补回。没有把聊天历史移到公共下载目录。

服务端使用 `device=desktop|android` 两个有界槽，旧客户端默认 desktop。
待认证令牌改为按 UID+token 隔离；Redis 路由、活动令牌、session ID 按设备槽隔离。
账号锁内重新验证槽位凭据后才替换旧连接；迟到清理只比较匹配的 session ID。
消息和好友通知向两个节点去重广播，消息包括发送者自己的另一端；持续历史游标仍由每个设备本地维护。
群媒体授权与资源令牌同样接受两个独立租约。不修改 MySQL 表结构。

构建脚本同时生成 `CppChat-Update-<版本>.json`。使用现有
`../CppChat/packaging/deploy-updates.py`
增加的 `--platform android` 发布到 `/updates/android/latest.json`，保留独立 Windows 清单。
清单必须指向同 HTTPS 源的 APK，含更高三段版本、精确字节数和 SHA-256；发布包先于 latest 可见，失败回滚。
后续只升级服务器程序不会虚报 Android 新版本；只有实际发布新的安卓安装包/清单才会提示更新。
覆盖升级必须使用相同包名和签名，并提高版本/versionCode；测试签名不是商店发行身份。

## 本轮来源补充

- [QtKeychain 0.17.0](https://github.com/frankosterfeld/qtkeychain/tree/875f77d9f61bd97fd84cca47ce3bc71186dfbd09)：BSD-3-Clause，源码及许可在 `third_party/qtkeychain`。直接复用 Android Keystore/Windows Credential Store 后端与异步 Job，禁用明文回退。该版 Android 使用混合 RSA/AES-GCM，避免旧版约 245 字节载荷限制；保留上游实现，不引入 Java 密码库或自制密码加密。
- [Qt 私有应用目录](https://doc.qt.io/qt-6/qstandardpaths.html)、[Android FileProvider](https://developer.android.com/training/secure-file-sharing/setup-sharing)：复用 Qt 标准路径及已部署的 FileProvider，不申请公共存储权限。
- [Tinode 多会话/同步模型](https://github.com/tinode/chat/blob/master/docs/API.md)：仅参考多会话投递的职责划分，不复制 GPLv3 源码、不引入 Go 服务；继续使用本项目现有 Redis Lua 租约、gRPC 和 MySQL 持久化。
- 先核对既有 `AutoLoginStore` 与 `ClientUpdateService`：前者显式限 Windows，因此 Android 凭据接入 QtKeychain；后者保留传输/校验/回滚能力，只增加平台参数，Windows 默认行为保持原样。

## 验证边界

基础版本的历史证据集中在两个项目各自的 `build/regression/mobile-foundation-20261023/`。
构建和 APK 签名/ZIP/89 个 ARM64 ELF 的 16 KiB LOAD 对齐检查通过；不含联调凭据。
原生凭据重启恢复/退出/清除、安卓清单类型和来源校验通过；QML 9 个页面三种尺寸共 27 场景无 QML 警告。
隔离 MySQL/Redis/SMTP/两个聊天节点已覆盖多端、令牌重放、替换、清理及现有业务回归，具体数量与当前结果见当轮报告。
Windows 上的门面/Qt 渲染验证不等于手机 APK 运行验收；Keystore 真机行为、软键盘、安全区、系统选择器和覆盖安装仍需实机验证。
上一轮 `android-20261023` 的联调记录仅作为历史证据，不代替本轮结果。

## 连接恢复与后台消息的边界

`MobileSession` 只管理登录意愿、重试时机和运行中的内存凭据；`appbridge_session.cpp`
把 Qt 网络/应用生命周期通知转成认证恢复，HTTP/TCP、历史持久化仍走共享后端。
普通断线保留当前账号、本地数据库、页面与草稿；每次恢复重新获取网关分配的节点和令牌，
完整认证并打开账号库后，沿用历史游标补拉漏收消息，不把重连当作主动退出。
重试间隔从 1 秒退避到 30 秒并加少量随机延迟；明确无网络时等待网络通知，恢复网络可立即重试。
回到前台时，后台超过 25 秒的旧连接重新认证，避免把仍显示 ConnectedState 的半开连接当成可用。
安卓已登录时，在聊天根页面按系统返回键会将任务移到后台；仅“退出登录”按钮清除登录意愿。
断线会停止录音/通话及旧资源请求；未确认发送仍保存在本地，可在同步后重试。
主动退出会取消登录 HTTP、清除内存会话凭据并停止重试；密码错误/账号失效、同平台被替换也停止重试，避免抢占循环。
网络错误、服务短暂故障不再关闭用户选中的自动登录。进程被回收后的重新登录需要此前已开启自动登录并成功保存系统安全凭据。
更新检测也补上“在后台错过六小时间隔后，回到前台检查”的实际回调。

系统休眠可能暂停 CPU/网络，不能保证 TCP 永远不断。用户确认目标为国内安卓多品牌、尚未开通过推送服务，
因此本轮不声称已完成进程被杀后的即时消息推送。后续需以相同包名/签名申请推送应用，
配置华为/荣耀、小米、OPPO、vivo 等厂商通道或聚合平台，再接入账号与设备绑定、退出解绑和通知点击补拉。
不能仅添加一个 SDK 或后台定时器就视为这条链路已完成。

本轮复用与检索依据：

- [Qt 6.12.0 Android 网络信息实现](https://github.com/qt/qtbase/blob/v6.12.0/src/plugins/networkinformation/android/qandroidnetworkinformationbackend.cpp)与[官方 QNetworkInformation 文档](https://doc.qt.io/qt-6/qnetworkinformation.html)：直接复用现有 Qt Network 的系统后端和信号，不复制上游源码、不增加网络库；当前 Windows MinGW 与 Android Kit 均为 6.12.0。许可证继续遵循现有 Qt 分发方式。
- [Android Doze/待机限制](https://developer.android.com/training/monitoring-device-state/doze-standby)：据此把网络断开与账号退出分开，避免以心跳/后台轮询承诺永久在线。
- 检索 [Nextcloud Talk Android](https://github.com/nextcloud/talk-android)后未引入其整套原生 UI、推送与服务器协议，避免与现有 Qt/CppChat 认证和持久化重复；重试策略只作为本项目门面的少量适配代码。
- [极光厂商参数申请](https://docs.jiguang.cn/jpush/client/Android/android_3rd_param)与[个推快速接入](https://docs.getui.com/getui/start/accessGuide/)确认多厂商通道依赖应用参数与平台开通；未取得配置前没有打包未初始化的 SDK、虚构投递结果或把服务器密钥放进 APK。

恢复测试用本机真实 TLS 套接字和现有协议帧，隔离服务产生合成账号/历史，并用现有加密 SQLite 驱动验证本地持久化。
它不替代 Android 真机的飞行模式、Wi-Fi/移动网络切换、Doze、进程回收和 Keystore 验收。

## 源码迁移与公开发布

运行产物、报告、历史副本和依赖统一留在被忽略的 `build/`；公开目录只保留 Android 前端、测试、必要构建定义及许可证。
签名密钥、实际 config.ini、个人 CMake presets、账号数据库、日志和 APK 不进入 Git，发布 APK 使用单独产物渠道。
部署配置沿用 [Qt 资源别名](https://doc.qt.io/qt-6/qt-add-resources.html)，没有增加第二套配置解析器或运行时依赖。
跨盘工作树迁移参考 [Git worktree repair](https://git-scm.com/docs/git-worktree)，完整复制并逐文件核对后修复登记路径。
公开快照使用 [Gitleaks v8.30.1](https://github.com/gitleaks/gitleaks/releases/tag/v8.30.1)（MIT）的上游规则检查，
工具从官方发布下载并核对上游 SHA256，仅用于本机诊断；另检查真实端点、联调账号和个人路径，
遵循 [GitHub 凭据管理说明](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository)。
