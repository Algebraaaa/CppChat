# Android 前端定向验证

这里只保存测试源码；全部构建、截图、日志和账号临时数据写入被忽略的 `build/`。

- `mobile_ui_smoke.cpp`：加载真实 QML，覆盖基础九页、文件/语音气泡、语音来电和视频来电；390×780、320×480、800×480 共 36 个场景。聊天气泡使用合成内容，不截取测试账号的已有消息。
- `mobile_media.cpp`：验证任意二进制/中文文件名与空文件往返、明确位置保存、损坏/超限/路径文件名拒绝、文件会话摘要，以及来电取消/断线后的设备所有权释放。不打开真实麦克风、摄像头或扬声器。
- `mobile_file_probe.cpp`：通过真实 AppBridge、MessageSender、加密 SQLite、ResourceMgr 和校验证书的 TLS，验证文件发送、回执、对方重新上传、下载和保存后字节一致。配套共享后端 `tests/integration/test_file_messages.py` 创建隔离双节点及两个测试账号；设置 `CPPCHAT_MOBILE_BUILD` 指向本机探测构建、`CPPCHAT_QT_BUILD` 指向含加密驱动的桌面构建。临时凭据与账号数据由夹具回收，不纳入 APK。
- `mobile_features.cpp`：验证安全凭据保存、重启恢复、主动退出关闭自动登录、清除凭据，以及安卓更新清单的类型/来源限制。Windows 主机验证不能替代 Android Keystore 真机验证。
- `mobile_session.cpp`：连接恢复策略及真实本机 HTTPS/TLS 门面测试；覆盖网络类型通知保留健康连接、断线保留登录/记录/草稿、服务暂不可用重试、漏收历史补拉、前台恢复、主动退出取消在途请求、顶号与密码错误停止恢复。仅使用合成账号和隔离数据库，不连接生产服务。
- `run_mobile_session.ps1`：生成一次性本机测试证书、配置加密 SQLite 插件并运行上述测试，测试结束清理证书和合成账号数据，失败日志保留。例如 `./tests/run_mobile_session.ps1 -BuildDirectory "$PWD/build/mobile-session-host" -SqlitePluginRoot $env:CPPCHAT_QT_BUILD -QtRoot $env:QT_HOST_PATH`。纯恢复策略可由 CTest 的 `mobile.session.policy` 单独执行；完整 TLS 用此脚本运行。
- `mobile_interop.cpp`：实际运行 AppBridge 与已联通的桌面后端源码，验证 HTTPS 登录、TCP TLS 认证、双向文字回执、图片上传/下载和 SHA256。账号及密码仅通过环境变量提供。
- `audit_android_apk.py`：检查交付 APK 的 ARM64 ELF、16 KiB LOAD 对齐、TLS 版本/依赖、证书外的私有文件排除；可通过 `--forbid-env` 扫描测试凭据，报告不输出敏感值。

探测程序通过 `CPPCHAT_MOBILE_TESTS=ON` 进行本机构建，不进入 Android APK。

聊天界面另有 `qml/tst_chatui.qml`，直接运行真实 QML，使用隔离的内存门面，不读取账号、密钥或业务数据库。
覆盖自适应气泡与纯文本、跨日/五分钟时间分段、群发送者、长按复制菜单、未发送附件禁用、
草稿成功保存后清空、离线/窄屏/横屏输入栏、阅读历史不跳动和返回最新、列表筛选，以及通话恢复文案的旧字段兼容。
390×780、320×480、800×480 的六张聊天/列表截图写入 `build/android-ui/screenshots/`。
截图是合成数据的主机布局检查，不替代 Android 真机的软键盘、安全区、触控选区和文件选择器验收。

```powershell
python CppChatAndroid/tests/run_chat_ui.py --qmltestrunner "$env:QT_HOST_PATH/bin/qmltestrunner.exe"
```

界面沿用已有 Qt Quick Controls（Qt 6.9+），参考 [Qt 6.9 官方聊天示例](https://doc.qt.io/archives/qt-6.9/qtquickcontrols-chattutorial-example.html)
的列表/委托与输入区分工，通过 `MessageBubble` 封装消息内容、`ChatStyle.js` 统一颜色及时间格式。
未复制示例源码或引入额外 UI 依赖，保留项目既有 Qt 许可；图标使用 Qt Canvas 绘制，不使用微信的商标或图形资源。
消息 `TextArea` 明确覆盖四边内边距，避免 Material 默认边距导致短气泡提前换行；长按菜单复用其 `selectAll/copy`，
测试采用 [Qt Quick Test](https://doc.qt.io/qt-6/qml-qttest-testcase.html)，任何运行期 QML 警告均计失败。
入口将截图与交互检查放入独立进程，避免主机菜单/视频窗口状态影响离屏回读；每组限时 30 秒，超时计失败并结束测试进程，日志和 JUnit 均保留。

`CPPCHAT_TEST_DATA` 必须指向 `build/` 内的隔离目录；不要让探测程序使用正式客户端数据目录。
两端用不同测试账号并设置 `CPPCHAT_TEST_PEER_UID`、同一个 `CPPCHAT_TEST_MARKER`，第二端设置 `CPPCHAT_TEST_ROLE=peer`。
`CPPCHAT_TEST_IMAGES=1` 增加双向图片验证。测试会发送带标记的消息，不执行数据库删除或服务变更。

结果保留最新三轮；旧报告完整压缩到 `build/regression/archive/`，校验归档后再清理原目录。
失败日志同样保留。收尾应停止本轮测试进程，清理隔离账号缓存、一次性构建与下载分块，保留最终 APK、校验值、必要证据、开发构建和依赖。
真机软键盘、系统返回手势/安全区、系统文件选择器、Windows GUI 与手机互发为人工验收项。
