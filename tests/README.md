# CppChat 回归测试

`client.features` 增补文件消息在真实聊天页中的收发显示、缓存读取、另存、显式打开及账号切换后拒绝旧文件回调；不启动真实外部程序。
`client.updates` 覆盖同次运行的暂停/断点续传、严格 `Content-Range`、服务器忽略 Range 时安全重下、重新检查版本时丢弃旧前缀，以及完整文件 SHA256 与有界进度通知。
移动端 `run_chat_ui.py` 覆盖千条聊天中回执不重建可见控件/不跳动阅读位置、待确认消息排序及附件控件复用后的状态隔离；这些主机测试不等于荣耀设备上的帧率验收。
消息分型回归另验证文字气泡、无文字背景的图片和独立文件卡片，保存三类消息同屏截图。移动端仅加载当前消息类型的组件；桌面文件卡片沿用现有图片组件跳过通用气泡绘制的方式。参考 [Telegram 文件渲染组织](https://github.com/telegramdesktop/tdesktop/blob/dev/Telegram/SourceFiles/history/view/media/history_view_document.cpp) 与 [图片渲染组织](https://github.com/telegramdesktop/tdesktop/blob/dev/Telegram/SourceFiles/history/view/media/history_view_photo.cpp)，仅参考职责划分，没有复制 GPL 源码或微信素材。

可选诊断程序 `update_download_probe <HTTPS清单URL> windows|android [采样字节数]` 直接运行共享更新器；正数到量后停止采样，负数到量后暂停并继续直至完整校验，省略则完整下载。只在显式运行时联网，最多 240 秒，不启动安装器，退出会清理所属临时文件。输出中只有 `verified_complete_package: true` 才代表完整安装包通过校验。
实现复用现有 Qt Network/QSaveFile/Qt Test，不增加第三方依赖；依据为 [Qt ListView 复用](https://doc.qt.io/qt-6/qml-qtquick-listview.html#reusing-items)、[Qt Quick 性能](https://doc.qt.io/qt-6/qtquick-performance.html)、[QNetworkRequest](https://doc.qt.io/qt-6/qnetworkrequest.html#setRawHeader) 与 [HTTP Range 规范](https://www.rfc-editor.org/rfc/rfc9110.html#name-range)。

`integration/test_file_messages.py` 的原生 TLS 往返使用 `qt/file_transport_probe.cpp`，
实际复用 AttachmentPreparer、MessageSender、ChatSyncService、加密 SQLite 和 ResourceMgr，
核验回执、另一账号重新授权上传、下载和保存后字节一致。主线用 `CPPCHAT_QT_BUILD` 中的
`file_transport_probe`，不依赖独立 Android 分支的 AppBridge；缺少探测程序仍记 BLOCKED，
不放宽必需测试的跳过门槛。Android 目录另保留其 MobileFacade 原生探测。

`client.call_stability` 使用实际 `VoiceCallService` 和可控单调时钟，验证已接通后的迟到媒体、
限流/短暂后端故障不误挂断、有效媒体恢复、长期失联结束、鉴权及显式挂断保持终止，
以及摄像头状态的 pong 补偿。不会打开设备。`integration/test_call_stability.py` 用真实双节点、
隔离 SQL/Redis 验证有效媒体续活、跨设备绑定、过期拒绝复活，以及通过私有 RPC 代理注入超时，
确保单帧/摄像头通知失败不删除有效租约。`test_voice_calls.py` 的原生 Qt 双客户端另包含
14 秒实际 TLS 转发停顿和重复媒体帧，要求通话不重拨并恢复 Opus/JPEG 解码，保存恢复次数与
实际最大收包间隔。该合成媒体故障注入不等于真人设备、不同运营商或 Wi-Fi/蜂窝切换验收。

`client.theme`（`qt/theme_tests.cpp`，`theme-junit.xml`）覆盖深浅色颜色/选区对比度、
旧偏好兼容与外观持久化、系统信号和手动覆盖、重复切换不增字体/观察器、草稿/笔记保留，
以及实际聊天、认证、设置、记录等窗口。CTest 明确选择可运行的跨平台函数；
原生 DWM 检查不属于 Linux/离屏计划，必须在 Windows 桌面额外执行
`theme_tests windowsNativeCaptionsUseDarkDwmAttributes`（`QT_QPA_PLATFORM=windows`），
查询真实属性。不得把未执行原生检查计为通过；失败和缺失验证分别记录。
全程临时数据，不更改系统主题或真实账号。
Qt 6.4 的只读注册表回退仍需旧 Kit 人工验收。

`client.settings_export`（`qt/settings_export_tests.cpp`，`settings-export-junit.xml`）覆盖
非秘密本机设置的持久化/写盘失败/未来版本保护、实际 Enter/Ctrl+Enter 和输入法预编辑事件、
设置入口和手动更新信号；以及真实明文文件的筛选、全页导出、新增行边界、容量限制、
取消重试、空权限、账号/关系变化、目标文件保护和保存选择器期间关闭账号。
使用临时库与文件，实际中文输入法、原生文件选择器和远程文件系统仍需人工验收。

`client.workspace`（`qt/chat_workspace_tests.cpp`，`workspace-junit.xml`）覆盖聊天页分离/收回的
实例所有权、草稿和会话切换、退出清空，以及全局记录来源、权限集合、字面搜索、类型/日期、
同时间分页与本机删除边界、并发历史窗口和深色调色板。用隔离数据库及离屏窗口，
不使用真实账号、硬件设备或云端；不把这项当作每联系人多窗口或多屏/DPI 人工验收。

`client.organizer`（`qt/organizer_tests.cpp`，`organizer-junit.xml`）验证独立笔记的 CRUD、
乐观版本检查、32 KiB/1000 篇上限、字面搜索及 50 条分页、失败写入、未保存确认和另存；
消息提醒的来源、时间/500 条上限、到期状态原子更新、重登恢复、去重、改期/取消和账号隔离；
通讯录真实表格筛选、星标、资料跳转及真实回环 TCP 备注保存，另测深色系统调色板可读性。
数据库夹具包含 v4 升级到 v5 后保留消息/收藏和 Windows 新正文加密验证。
不使用真实账号或系统通知，也不以自动截图替代实际桌面、多屏和长时运行验收。

`client.conversations`（`qt/conversation_management_tests.cpp`，`conversations-junit.xml`）覆盖
会话免打扰的菜单/开关同步、静音图标与侧栏红点、保留未读、服务器/账号隔离、
隐藏后保留草稿和记录、刷新不复活、新消息/主动打开恢复、确认取消、群关系保留、
本机删除后重开和旧历史过滤、确认期间出现待确认消息及切换账号、偏好写入失败。
测试只使用临时加密数据库和 QObject 生命周期，不发起生产网络请求。

`client.voice` 验证呼叫状态、拒接/取消/断线、错误与过期事件、序号去重、坏帧及真实 Opus 编解码/PLC。
`integration/test_voice_calls.py` 使用隔离 MySQL/Redis、两个真实聊天节点，覆盖同节点/跨节点、
双向媒体、接听权限、非好友、忙线、重放及登录替换；`voice_transport_probe` 用两个实际 Qt 客户端
运行生产 TcpMgr/VoiceCallService/Opus 代码，以合成音源双向发送并解码至少 40 帧。
该探测覆盖显式开发模式的回环 TCP，以及独立 CA 和证书验证的回环 TLS，
**不等于公网、真人麦克风、回声或弱网通话验收**。
使用 `CPPCHAT_QT_BUILD` 选择探测构建，缺少探测程序记 BLOCKED；本轮私有登录配置结束即删除。
真实设备、麦克风权限、扬声器、不同网络延迟和长时负载需另外验收，建议使用耳机。

统一入口：`python tests/run.py --suite smoke|full|nightly|release`。业务构建默认关闭测试；测试不会读取 `.runtime`、开发配置或生产数据。所有 SQL 仍在 `database/`，测试只引用脚本。

`client.features` 覆盖拼音分组、备注优先/清除、真实回环 HTTP/TCP 登录与备注保存、Windows DPAPI 凭据隔离/防篡改、成功登录后保存、取消、密码失效和退出清除；非 Windows 验证禁止密码落盘。
联系人增量覆盖星标真实操作、折叠与选择、重登保存、服务器/账号隔离、删好友清理和写盘失败；
资料页两种通话经实际主窗口和回环 TCP 发起并取消，不打开设备。
共同群聊经回环 TCP 验证分页、失败重试、打开群及请求互斥；
`integration/test_groups.py` 另用真实双节点/MySQL 验证双方资格、非好友拒绝、超过 50 个群分页、
整页边界、解散/移出后即时撤销。自动通话验证不替代真人设备验收。
`client.updates` 另覆盖仅显示高于当前版本且不超过清单版本的更新记录、数字版本降序、已是最新版/本机更高版本隐藏记录、旧清单兼容、重新检查失败后清空，以及结构化历史校验。打包历史通过
`tests/packaging/test_release_history.ps1 -Artifacts build/release-history-tests` 验证，已接入 Windows CI。

`client.messages`（`qt/message_actions_tests.cpp`，输出 `messages-junit.xml`）覆盖实际右键菜单的
纯文本放大阅读、复制和字号调节、引用保留选区草稿/图片与一次撤销、Unicode/长文截断、
本机删除取消/确认、失败事务回滚、搜索与会话摘要刷新、历史重放/重复通知不复活、
清空历史后的晚到回执核销、v1 加密库升级和拒绝未来版本、重开/账号隔离及过期菜单边界。
通过网络信号验证本机操作不发送请求；数据由 QTemporaryDir/ChatStore fixture 隔离回收。
自动截图不能替代真实多屏、DPI、系统剪贴板及安装包人工验收。

2026-10-19 新增 `client.favorites`（`qt/favorites_tests.cpp`，`favorites-junit.xml`）：
加密收藏快照、原记录删除后的独立保存、账号/服务器隔离、重开、去重、字面搜索、分类分页、
媒体校验及事务回滚，实际列表、预览及重新发送入口。`client.messages` 增加真实多选工具栏、
批量收藏/转发/删除及删除事务失败后重放；`client.features` 增加回环 HTTP/TCP 登录后的
目标选择、新 UUID、草稿保留、延迟及取消回包，以及按新收件人重新上传图片的失败恢复。
测试目录按用例隔离，数据库线程关闭后统一删除，不使用日常账号和配置。

`integration/test_group_images.py` 使用真实双节点、SQL/Redis 和隔离资源目录验证
群图片续传、校验、去重、跨节点通知、离线历史、跨群及私聊范围拒绝、非成员和退群/移除/解散
后的读写撤销。单独故障夹具验证授权节点回退及全部不可用时拒绝访问。
`client.features` 覆盖旧服务器能力门控、群上传目标和真实 PictureBubble 解码显示。
自动验证使用合成图片，不等同于双桌面人工及弱网体验验收。
回环监听检查使用 Python 标准库 `ipaddress` 识别 Linux gRPC 的 IPv4-mapped IPv6
表示，仍严格要求实际地址为 `127.0.0.1`，拒绝通配地址；依据
[gRPC POSIX 实现](https://github.com/grpc/grpc/blob/v1.81.1/src/core/lib/iomgr/tcp_server_posix.cc)
及 [Python 官方映射地址接口](https://docs.python.org/3/library/ipaddress.html#ipaddress.IPv6Address.ipv4_mapped)。

语音消息增量：
2026-10-19 语音消息覆盖：`integration/test_audio_messages.py` 使用标准库 `wave` 生成
合成 1/60 秒 PCM，真实上传、双节点收发、去重、离线历史、旧端升级提示、权限撤销，
拒绝时长、通道、采样率、编码和长度损坏。`client.voice` 验证固定容器边界及通话互斥；
`client.features` 验证协商、群语音按新目标上传和实际语音气泡，打开录音窗口不启动设备；
`client.favorites` 验证加密音频快照、原消息删除、重开和账号隔离，并防止音频误入链接搜索。
这些测试不打开麦克风或扬声器，不能代替真实设备录放和弱网验收。

首次准备 Python 3.12+ 虚拟环境和锁定依赖：

```powershell
python -m venv build/test-env
build/test-env/Scripts/python -m pip install -r tests/requirements.lock
npm ci --prefix VerifyServer --ignore-scripts
```

Linux 对应 `build/test-env/bin/python`。入口自动使用项目虚拟环境。未安装依赖或 Docker 未运行会生成 BLOCKED 报告；不会自动操作本机数据库。Node 直接依赖固定版本，完整依赖在 `VerifyServer/package-lock.json`；GoogleTest/GoogleMock 固定到 v1.17.0 发布提交，仅测试构建下载。

Windows 首次配置两个独立构建目录：服务端沿用 MSVC/vcpkg 工具链，客户端沿用自己的 Qt Kit。已配置工程可直接运行入口；新环境先向 CMake 提供对应工具链/Qt 路径。客户端支持 `--config <file> --data-dir <directory> --log-dir <directory>`，测试覆盖数据、附件、会话偏好、表情和日志路径；指定数据目录时不迁移日常使用的旧库。

```powershell
python tests/run.py --suite smoke
python tests/run.py --suite full
python tests/run.py --suite nightly
python tests/run.py --suite release --configuration Release --qt-build CppChat/build/release --manual-results build/manual-results.json
```

`--no-build` 仅复用已启用 `CPPCHAT_BUILD_TESTS=ON` 的构建。可通过 `--server-build`、`--qt-build`、`--cmake`、`--qt-cmake` 指定位置和同一 Kit 的工具。CTest 拒绝空测试集合；入口不以历史 XML 替代当前执行结果。

每轮报告在 `build/regression/<时间-随机ID>/`：总览 `index.html`、`summary.json`、`combined.xml`，两种 Python 模式各自的 pytest HTML/JUnit，GoogleTest/Qt JUnit，日志、截图、随机种子和依赖信息。默认保留最新三轮完整目录，旧报告压缩到 `build/regression/archive/`，校验归档完整后才删除展开目录，失败证据也保留。`--keep-runs N` 调整展开数量；未完成运行和无法识别的目录不自动清理。退出码 0=PASS，1=FAIL，2=BLOCKED。跳过必测项按 BLOCKED 统计；占位功能实现状态为“未实现”，其当前交互仍需要通过。测试不自动重跑失败用例。

文件按用途归类，运行产物均不提交：

| 位置 | 内容与处理方式 |
|---|---|
| `tests/unit`、`cpp`、`qt`、`node` | 单元及组件测试源码，保留 |
| `tests/integration`、`regression` | 真实服务功能、故障及验收测试源码，保留 |
| `tests/harness`、`ci`、`performance`、`manual` | 隔离环境、CI、压测及人工验收工具，保留 |
| `database/` | MySQL / SQLite 完整建表 SQL，独立于业务源码 |
| `build/regression/` | 最新报告、历史归档、当轮日志与截图 |
| `build/test-cache/` | pytest、Ruff 可再生缓存，可删除 |
| `build/test-env/`、`VerifyServer/node_modules/` | 已安装测试及运行依赖，保留以复用 |
| `build/server/`、`CppChat/build/` | 日常开发构建，保留以复用 |

诊断时请将日志和临时构建放入 `build/`，不要放在源码目录。一次性验证构建用完可删除；删除后再次运行对应 Linux/覆盖率任务需要重新构建。统一入口不在 `tests/` 写 Python 字节码或 pytest 缓存。

Windows 安装包的定向验证入口为 `tests/packaging/verify-installer.ps1 -Installer <安装包> -Manifest <payload-manifest.json>`，先构建 Qt 测试目标 `package_probe`。它在 `build/packaging/` 的独立目录测试安装、文件哈希、快捷方式、剔除开发 PATH 后的 Qt/SQLite/TLS 运行、升级和卸载的数据保留；探测实际 `CppChat.exe --version` 与编译版本一致，不发邮件或访问真实账号。已有 CppChat 安装记录（HKCU/HKLM、32/64 位视图）时拒绝覆盖，请使用干净用户环境；无注册记录时使用随机独立开始菜单组，校验原 CppChat 快捷方式哈希不变。成功后回收自身测试安装和临时数据；失败日志保留，失败安装应核实所属路径后卸载。该验证不替代完整云端业务回归或跨机器验收。

客户端更新的隔离回归目标为 `update_tests`（CTest `client.updates`）：脚本化 HTTPS reply 验证版本、URL 信任边界、重定向/损坏/截断/超长/TLS 失败、取消/重试/超时、界面与原安装路径；Windows 原生 `installer_handoff_probe` 只记录真实进程交接参数，不安装程序。`python -B -m unittest discover -s tests/packaging -p test_publish_updates.py` 验证发布事务、不可覆盖版本、拒绝倒退及回滚，不接触真实 SSH/Nginx。`update_probe <HTTPS清单URL> <当前版本> [--download]` 验证真实证书、在线清单及安装包下载校验，不启动安装器，结束时删除自身下载；只能在明确发布后用于当前在线验证。报告仍写 `build/`，不把隔离 transport 测试当成真实网络证据。

Windows 安全回归目标 `security_tests`/CTest `client.security` 验证正式传输策略、真实 AES-GCM/DPAPI、加密 DB/WAL、拒绝普通 SQLite 读取、迁移与恢复、密钥缺失及密码恢复、私有 ACL；原有客户端回归使用明确的本机开发传输策略，发布目标保持严格策略。`security_store_probe create|audit <账号目录>` 仅用于隔离夹具及记录数/摘要核对，create 拒绝覆盖现有数据库，audit 不输出消息内容。安装验证会创建自身明文旧聊天/草稿/附件，升级后验证内容摘要一致且文件已加密，卸载后密钥保留。已注册的用户安装不用于该破坏性夹具验证。受信任代码签名需真实证书，当前仅验证签名强制开关及静态配置流程。

套件内容：

| 套件 | 自动范围 | 必须另行记录的证据 |
|---|---|---|
| Smoke | 两套本机测试、验证码处理器、核心真实 SMTP/认证/跨节点图文/离线/重置/删除链路、协议边界 | 无 |
| Full | 全部非故障自动用例、数据库兼容、Qt/HTTP TLS、启动配置 | 桌面人工部分仍在报告中列为待验收 |
| Nightly | Full + Hypothesis、并发重置、MySQL 过期/KILL/冷启动/恢复、Toxiproxy 故障 | 长时性能测量使用下述入口；ASan/UBSan/TSan 及覆盖率由独立 CI 构建执行 |
| Release | Debug/Release 构建及本机测试、全部自动回归 + Windows 人工清单 | Debug 和 Release 两种构建、发布包与真实桌面证据；缺少即 BLOCKED |

`cases.json` 映射自动及人工用例，每项记录前置、输入、响应、数据变化、清理方法。人工步骤在 [manual/windows.md](manual/windows.md)，运行 `python tests/manual/prepare.py --output build/manual-results.json` 生成待验收记录。需要验收人、时间、Kit、编译器、DPI、非空证据，以及相同源码指纹；历史文档中的“通过”不被接受。

真实依赖采用 Testcontainers：MySQL 8.4.0、Redis 7.4.2、Mailpit 1.21.8、Toxiproxy 2.9.0。每个 Python 模式独立容器/随机账号/动态端口，启动五个 C++ 二进制和一个 Node 进程。故障用例另外独立运行，避免污染普通用例连接池。失败、正常退出都会回收自己创建的进程和容器；请勿用系统进程名批量结束服务。容器端口不使用本机 MySQL 3306。初始化只执行 `database/mysql/schema.sql`，验证九张业务表、标签和图片字段、重试索引及重复导入保留数据。

TLS 使用当轮 CA、私钥、有效/过期/不可信证书和回环代理；Qt probe 使用同一 Kit 的 QSslSocket，严格验证主机名，不调用 `ignoreSslErrors()`。私钥是运行产物。截图计算覆盖四种缩放，真实多屏、焦点、系统剪贴板和发布依赖必须人工验收。

Linux 构建直接使用根 CMake 和版本控制内源码。可用 `tests/ci/Dockerfile` 复现依赖环境；`bash tests/ci/build-linux.sh` 构建服务端和 Qt。CI 在 PR 运行 Smoke、push 运行 Full、夜间运行随机/故障，单独运行 ASan、UBSan、适用的 TSan；覆盖率使用单独 `CPPCHAT_COVERAGE=ON` 构建并执行 80% 行/70% 分支门槛，不将尚未达到门槛的报告记为通过。

性能：

```powershell
build/test-env/Scripts/python tests/performance/run.py --mode collect --profile baseline --server-build build/server --artifacts build/performance
build/test-env/Scripts/python tests/performance/run.py --mode collect --profile soak --server-build build/server --artifacts build/soak
python tests/run.py --suite nightly --performance baseline --approved-baseline <approved.json>
```

默认 100 连接、每连接 1 条/秒、600 秒，或 50 连接、7200 秒，三次取中位数。k6 测 HTTP，asyncio 复用功能编解码器测真实双节点聊天；检查每条回执、送达和唯一落库，记录吞吐、P95/P99、内存、线程、句柄/FD、在途积压，保存服务端队列指标日志。`--connections/--duration/--runs` 仅诊断，不可冒充约定基线。`--mode collect` 成功完成测量后输出 `COLLECTED`、退出码 0，并明确记录 `comparison_status: NOT_RUN`、`approved: false`；这表示采集成功，不表示性能回归比较通过。夜间及每周稳定性工作流采用此模式。默认 `--mode compare` 保留严格比较语义，缺少 `--approved-baseline` 时在压测前返回 BLOCKED/2；提供带 `approved: true`、`approved_by` 的同环境记录后，P95 超过 20% 退化判 FAIL。无效基线、环境不匹配、接收错误、消息丢失/重复或 k6 阈值失败仍阻止通过；不会自动批准候选。GitHub Summary 直接显示采集或比较结果。每周两小时稳定性在独立长时 CI 作业执行，不占 PR。

`--resource-every N` 可在同一负载中每 N 秒为每对账号增加一次 32 KiB 资源上传及接收方下载；独立记录资源 P95/P99 并校验 SHA256。它改变负载模型，必须使用相同模型的批准基线。基础聊天基线默认不附加图片负载。

在 Docker Desktop 内的 Linux 构建容器里运行集成时，需要挂载 Docker socket 并设置 `TESTCONTAINERS_HOST_OVERRIDE=host.docker.internal`；GitHub 的原生 Linux runner 使用默认主机探测。该覆盖只指向本机测试容器宿主。

构建容器与依赖容器在同一 Docker bridge 时可加 `CPPCHAT_TEST_CONTAINER_NETWORK=bridge`，通过当前容器地址访问依赖，减少 Docker Desktop 端口转发的影响；它不会访问宿主数据库。地址会在容器启动后重新读取。覆盖率运行前清空当轮构建的 `.gcda` 计数，不累加历史验证结果。

GitHub 方法参考：[GoogleTest](https://github.com/google/googletest)、[Qt Test](https://github.com/qt/qtbase/blob/dev/src/testlib/doc/src/qttestlib-manual.qdoc)、[pytest](https://github.com/pytest-dev/pytest)、[Testcontainers](https://github.com/testcontainers/testcontainers-python)、[Hypothesis](https://github.com/HypothesisWorks/hypothesis)、[Toxiproxy](https://github.com/Shopify/toxiproxy)、[Mailpit](https://github.com/axllent/mailpit)、[k6](https://github.com/grafana/k6)。

覆盖率门槛统一使用源码普通条件分支，按 [gcovr 官方说明](https://gcovr.com/en/stable/faq.html#why-does-c-code-have-so-many-uncovered-branches) 排除编译器生成的异常展开和不可达分支；异常处理本身仍由数据库/网络故障及抛出任务用例验证。门槛仍是行 80%、分支 70%。原始 CFG 统计可以去掉 `--exclude-throw-branches --exclude-unreachable-branches` 单独导出，用于检查差异；不会将两种口径混用为同一基线。

性能采集与比较分离参考 [k6 自动化测试](https://grafana.com/docs/k6/latest/testing-guides/automated-performance-testing/)；保留现有锁定 k6 0.57.0 及协议负载工具，不增加依赖。CI 摘要使用 [GitHub Job Summary](https://docs.github.com/en/actions/reference/workflow-commands-for-github-actions#adding-a-job-summary)。`test_performance_reporting.py` 覆盖采集退出状态、批准/环境校验、20% 边界、真实压测失败传播及稳定性聚合。


`client.groups` 覆盖群入口、群名/发送者显示、真实回环 TCP 创建与管理响应、加密库重启恢复与成员撤销。
群聊交互回归还覆盖搜索不丢多选、整行勾选/取消、自动群名和人数上限，批量邀请部分失败后的保留与重试，
群主转让后的控件权限，以及通讯录群入口/改名/不重复刷新、标题打开详情、备注与消息摘要。
小屏幕选人内容滚动，列表保留可用高度且关闭按钮留在窗口内；可用 `QT_SCALE_FACTOR=1.5|1.75|2`
在 offscreen 模式下验证布局与交互，这些截图不替代 Windows 多屏/原生 DPI 的人工体验验收。
`client.video` 覆盖 JPEG 体积/尺寸边界、格式拒绝、通话类型同意、摄像头开关与迟到帧，
以及来电/关窗前不打开摄像头；`integration/test_video_calls.py` 验证真实节点的同/跨节点
路由、未接听、重放、超限、关闭摄像头和旧会话拒绝。`test_voice_calls.py` 的 Qt 双客户端
探测新增 TCP/TLS 视频模式，实际编解码合成 JPEG 与 Opus，不打开真实摄像头或麦克风。
这些自动验证不代替真实设备、权限拒绝、热拔插、回声、音视频同步和公网体验验收。
`integration/test_groups.py` 使用两个实际聊天 EXE、隔离 MySQL/Redis，覆盖群主权限、好友邀请、
跨节点和同节点投递、消息重试去重、离线补拉、移出后禁读禁发、转让/退群/解散、非法参数和 32 人边界。
群图片尚未开放，测试要求拒绝而非放宽现有资源服务的两人 ACL。

服务端日志专项：`server.logging` 通过实际原生进程覆盖等级过滤、UTF-8 路径、固定文本、线程并发与退出排空、
大小轮转/槽位复用、并发进程和强杀后的锁释放、队列溢出重要日志保留、不可写路径回退、空闲/指标增量限频。
Windows 的 `server.log_viewer` 验证启动脚本解析、追加读取、文件身份轮转（即使新文件更大且时间戳相同）和句柄释放。
两项已接入根 CMake/CTest；独立运行可用 `cmake -S tests/logging -B build/logging-tests` 后构建并执行 CTest。
测试仅在该构建的 artifacts 下创建临时文件，回收自己创建的进程和数据，不连接 MySQL/Redis 或生产端点。
