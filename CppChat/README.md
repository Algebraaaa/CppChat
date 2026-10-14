# CppChat

C++ / Qt Widgets 聊天客户端。聊天业务通过 ChatServer 处理，图片原始字节通过
ResourceServer 传输，本地消息和草稿保存在按服务端及账号隔离的 SQLite 数据库。

## 开发与维护入口

- 在 Qt Creator 中打开本客户端目录的 `CMakeLists.txt`，选择对应 Qt Kit 和 Debug / Release 配置后构建。
- `CMakeLists.txt` 管理客户端源码、UI、资源和 Qt 运行库部署；`.ui` 管布局，QSS 管外观，C++ 管行为。
- [项目目录与约定](docs/project-guide.md)
- [界面功能](docs/ui-features.md)与[本地聊天记录](docs/local-chat-history.md)
- [性能优化实现和验证](docs/performance-improvements.md)
- [架构加固阶段与验证](docs/architecture-hardening-20261006.md)和[客户端流程服务](services/README.md)
- [服务端架构](docs/backend-architecture.md)与[资源服务构建配置](../ResourceServer/README.md)

本机开发配置、真实账号数据库、个人图片缓存、日志和中间编译产物不应直接发布。安装包只携带筛选后的公网连接参数和运行依赖。

## CMake 构建

仅客户端采用此构建入口，上层服务端工程保持独立。需要 CMake 3.24+、Ninja、C++17 编译器和 Qt 6 的 Core、Gui、Widgets、Network、Sql、Svg、Concurrent 模块。

在客户端目录执行：

```powershell
cmake --preset debug -DCMAKE_PREFIX_PATH=D:/Qt/6.12.0/mingw_64
cmake --build --preset debug
```

`CMakePresets.json` 提供 `debug` / `release`。本机的 `CMakeUserPresets.json` 配置了已安装的 Qt、MinGW 和 Ninja，可直接使用 `local-debug` / `local-release`，或在仓库根目录执行 `CppChat.cmd build -Target Client -Configuration Debug`。Debug 与 Release 分别输出到本客户端的 `build/debug`、`build/release`。Windows 链接后自动部署当前 Qt Kit 的 DLL、插件和 SQLite 驱动。

本机配置为客户端根目录的 `config.ini`，修改它后重新运行 CMake，即会同步到 Debug / Release 的 EXE 所在目录。客户端实际读取 EXE 同级的 `config.ini`。配置已被 Git 忽略。

Windows EXE 图标由 `app/app.rc` 嵌入现有 `Whatsapp.ico`，与 Qt 窗口的 `:/res/app.ico` 保持一致；修改 ICO 后重建客户端即可更新。这采用 [Qt 6.12 官方 Windows 图标部署方式](https://doc.qt.io/qt-6/appicon.html)，资源文件仅加入 Windows 客户端目标，不增加第三方依赖。

## Windows 安装包

在仓库根目录执行：

```powershell
.\CppChat.cmd package -Version 0.1.2
```

也可双击 `CppChat.cmd` 选择 `10`。安装包输出为 `build/packages/CppChat-Setup-0.1.2-x64.exe`，同目录提供 SHA256 文件及 `CppChat-Update-0.1.2.json`；更新版本时改变 `-Version`。脚本先配置 `CPPCHAT_APP_VERSION` 再构建，使客户端、安装器和更新清单版本一致。其他用户只需下载该安装包，运行安装向导，不需要安装 Qt 或开发工具。

打包入口是 `packaging/build-installer.ps1`，安装规则是 `packaging/CppChat.iss`。脚本使用现有 Release 构建缓存选择匹配的 Qt Kit，调用 [Qt windeployqt](https://doc.qt.io/qt-6/windows-deployment.html) 收集 DLL 和插件，再调用 [Inno Setup](https://jrsoftware.org/)；本机验证版本为 Qt 6.12.0 / MinGW Kit、Inno Setup 6.7.3。使用上游原生部署/安装能力，适配范围为运行库清单、配置白名单、用户安装模式、快捷方式和数据保留，没有修改第三方源码；运行库及现有字体/图标许可随包分发。Inno Setup 从官方 `is-6_7_3` 发布下载并验证 Pyrsys B.V. 数字签名，编译器安装在 `D:/Dev/tools/inno-setup-6.7.3`；脚本通过 PATH 或当前用户安装记录查找它，其他位置可直接调用脚本并传入 `-IsccPath`。

默认安装位置是当前用户的 `AppData/Local/Programs/CppChat`，无需管理员权限，也可选择其他可写目录。发布版聊天数据、图片缓存仍在所选安装目录的 `chat_data`，日志位于 `logs`。升级不覆盖用户生成的数据，卸载仅移除安装器登记的程序文件，保留 `chat_data`、日志和已有配置；这些保留文件可由用户自行备份或处理。

打包配置只读取 `GateServer`、`ChatServer` 和 `ResourceServer` 的公网地址、端口及 TLS 设置，拒绝打包指向 localhost 的配置。仅部署 SQLite 驱动和现有 Schannel TLS 后端，不收集其他数据库驱动、本机账号、服务端配置或邮件授权码。编译记录和文件清单位于 `build/packaging/`，成功后回收临时 payload；安装包尚未进行应用代码签名。

2026-10-13 已验证真实安装、开始菜单快捷方式、逐文件哈希、升级及卸载后的数据保留；在移除 Qt 开发目录 PATH 和插件路径的进程中，界面主题、字体/图片/图标、SQLite、TLS 和公网配置探测均通过。验证源码在 `tests/packaging/verify-installer.ps1`、`tests/qt/package_probe.cpp`，证据保存在 `build/packaging/`。这不等同于干净虚拟机或其他电脑上的完整云端注册、跨节点聊天验收。

### 客户端更新

Windows 客户端在窗口显示两秒后异步检查更新，也可点击标题栏的“检查更新”。入口由当前 HTTPS GateServer 地址推导为 `/updates/windows/latest.json`，不会要求用户手动修改原有 `config.ini`；本机 HTTP 开发模式不执行自动检查。检查失败只记录日志，手动检查会显示失败信息。发现更高的三段数字版本后，显示更新说明；用户决定下载和安装，取消下载会移除未完成文件。安装前验证字节数与 SHA256，并在启动安装器前再次验证，主窗口析构完成草稿保存、会话和 SQLite 关闭后才启动安装器；`/DIR` 指向原 EXE 目录，保留原来的安装向导及聊天数据。

正常发布流程（每次递增版本）：

```powershell
.\CppChat.cmd package -Version 0.1.2 -ReleaseNotes "修复聊天显示问题"
.\CppChat.cmd publish-update -Version 0.1.2
```

`publish-update` 显式发布到已有 `huawei` SSH 主机：先校验本机清单与安装包，上传到专用 staging，云端再校验，保留 Nginx 配置和旧清单备份，执行 `nginx -t` 后发布完整安装包、原子替换 `latest.json` 并平滑 reload。已发布安装包不可用同版本不同内容覆盖；拒绝将更旧版本设置为 latest，发布锁防止并发倒退；失败回滚配置及清单并保留 staging 证据。仅增加现有 HTTPS 站点的更新静态路由，不重启业务服务或变更数据库。默认目录 `/opt/cppchat/updates/windows`，备份 `/opt/cppchat/backups/updates-*`，本机记录 `build/packaging/publish-*`。此部署入口针对当前 Ubuntu/Nginx 布局；更换服务器布局时需适配发布脚本，不能直接执行首次 provision 流程。

更新清单包含 `version`、纯文本 `notes`、相对 `downloadUrl`、64 位十六进制 `sha256` 及 `size`。客户端只接受同一 HTTPS 主机/端口、不含凭据/查询/片段的 EXE 地址，不跟随重定向；清单最多 64 KiB、安装包最多 512 MiB。Qt 原生 TLS 校验证书，不忽略 SSL 错误。下载使用 `QSaveFile`，验证完成才提交；普通退出移除本轮下载，仅成功交给安装器的文件保留在用户缓存中供安装器读取。SHA256 用于完整性校验，发布信任依赖 HTTPS；安装包仍未进行应用代码签名。已安装 0.1.0 的用户需先手动安装 0.1.1，之后才有更新通知。开发构建不要通过安装器覆盖构建目录。

实现分为 `services/ClientUpdateService`（版本/网络/校验/安装交接）、`dialogs/UpdateDialog`（提示与进度）和 `main.cpp`（退出顺序），不复用登录请求的业务编号和模块分发。检索了 [QtAutoUpdater](https://github.com/Skycoder42/QtAutoUpdater)（BSD-3-Clause，有上游 tests，README 仍为 Qt 5 的构建及分发方式并请求维护者）与 [Qt Installer Framework](https://doc.qt.io/qtinstallerframework/ifw-overview.html)（需改为维护工具及组件仓库）；两者会增加 Qt 6.12/MinGW 或现有 Inno 包的适配成本，因此不复制第三方更新代码。复用已安装 Qt 6.12 的 [QNetworkAccessManager](https://doc.qt.io/qt-6/qnetworkaccessmanager.html)、[QVersionNumber](https://doc.qt.io/qt-6/qversionnumber.html)、[QSaveFile](https://doc.qt.io/qt-6/qsavefile.html)、[QProcess](https://doc.qt.io/qt-6/qprocess.html) 与 Inno Setup 6.7.3 [原生安装参数](https://jrsoftware.org/ishelp/topic_setupcmdline.htm)，无新增依赖或第三方源码许可变更。

2026-10-13 更新模块验证：Debug/Release 更新测试各 33 项、既有客户端回归 40 项、发布事务测试 6 项通过；真实安装包验证实际 EXE 版本、53 个 payload 文件、独立快捷方式组、隔离 Qt/SQLite/TLS 运行及升级/卸载数据保留。云端已发布 0.1.1，真实 HTTPS 探测确认同版本不提示、模拟旧版本提示，并完成 39,947,032 字节安装包下载及 SHA256 校验。证据在 `build/regression/diagnostics/client-update-20261013/` 和 `build/packaging/`；窗口截图由 offscreen Qt 生成，不等同于实际桌面 DPI/辅助技术或干净虚拟机验收。初次测试的 Win32 include 顺序、测试删除断言问题及安装验证保护拦截均保留日志，修正后以最终报告为准。

### Windows 本地数据与发布安全（0.1.2）

正式客户端的传输策略单独编译进 EXE：必须使用 HTTPS，聊天/资源连接必须启用 TLS，登录请求仅允许同源重定向。Debug 仅允许回环地址使用明文；测试本机 HTTP 策略只链接进 `client_tests`，不会进入发布 EXE。打包脚本也拒绝关闭任一 TLS 开关。

安装器在启动客户端前保护整个安装目录及现有 `chat_data`：NTFS ACL 收紧为当前 Windows 用户、SYSTEM 和管理员，拒绝重解析链接及驱动器根目录，不自动取得他人文件所有权。数据仍在用户选择的安装目录，Windows 家庭版可用；不支持这些 ACL 的文件系统不适合正式安装。不能防范管理员或以本人身份运行的恶意程序。

Windows 数据库及 WAL 使用 `sqldrivers/CppChatSqliteCipher.dll` 加密，每个账号独立随机 256 位密钥，以当前 Windows 用户 DPAPI 密文保存为 `.key.dpapi`；附件使用 Windows CNG AES-256-GCM。密钥或驱动丢失、篡改时拒绝解密，不生成替代密钥或回退明文。旧库先检查 WAL、生成快照，在受限目录加密并核验，保存 `.security-backups/<时间-UUID>/` 后才原子替换。旧库被占用时保留可恢复数据并要求退出其他客户端。已迁移后普通 SQLite 工具/旧版客户端不能直接读取，不能靠覆盖旧 EXE 回退。

关闭客户端后，可将加密账号快照恢复到新空目录；同一 Windows 用户/电脑可直接恢复，拒绝覆盖现有数据：

```powershell
& 'D:\SoftWare\CppChat\CppChat.exe' --restore-local-backup '<账号目录>\.security-backups\<备份名>' --restore-target '<新空目录>'
```

换电脑或重置 Windows 用户之前，要同时备份 `chat_data` 和密码保护的恢复密钥包。以下命令弹出密码输入框，不在命令行、日志或 INI 保存恢复密码；至少 12 字符，导出确认两次。密钥包采用 PBKDF2-HMAC-SHA256（300000 次、随机盐）及 AES-GCM。导出拒绝覆盖现有文件，导入先核对全部密钥与已复制的数据库，再重新绑定当前 Windows 用户；旧绑定文件保留为 `.key.dpapi.previous`。

```powershell
& 'D:\SoftWare\CppChat\CppChat.exe' --export-recovery-keys '<备份目录>\CppChat-Recovery.json'
# 新电脑复制 chat_data 并安装新版后，用相同恢复密码导入：
& 'D:\SoftWare\CppChat\CppChat.exe' --import-recovery-keys '<备份目录>\CppChat-Recovery.json'
```

路径示例需替换为实际位置，`--data-dir` 可指定数据根目录。恢复密码与 Windows 密钥都丢失时无法绕过加密；旧快照的 DPAPI 密钥仍绑定原 Windows 环境，跨环境恢复应先将选定快照数据放回对应账号目录，再导入密码保护的恢复密钥。Windows 加密不声称消除磁盘过去的明文残留。非 Windows 客户端继续原有存储方式，尚未实现 Linux 密钥保护；服务端数据库不受影响。

签名模板 `packaging/code-signing.example.json`，实际配置放在被忽略的 `.runtime/code-signing.json`，或传 `package -SigningConfig <本机JSON> -RequireSigning`。仅引用 Windows CurrentUser/My 证书指纹、SDK SignTool 和 HTTPS 时间戳服务，私钥不进入代码、安装包或命令行。流程：strip → 签名并验证客户端/加密插件 → Inno 签名 Setup/Uninstall → 最后生成 SHA256/清单。配置、证书、工具、时间戳或验证失败则中止，`-RequireSigning` 在无配置时拒绝打包。当前没有证书，0.1.2 实际未签名，尚未进行受信任证书实签验收。

复用来源：[SQLite3 Multiple Ciphers 2.6.1](https://github.com/utelle/SQLite3MultipleCiphers/releases/tag/v2.6.1)（MIT、SQLite 3.54.0）、[Qt 驱动固定提交](https://github.com/FalsinSoft/QSQLite3MultipleCiphers/tree/65accf531f33952c3431d914fcdf7f6c6cd02c35)（保留 Qt LGPL-3.0 等许可）、[DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata)、[CNG PBKDF2](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptderivekeypbkdf2)、[Inno 原生签名](https://jrsoftware.org/ishelp/topic_setup_signtool.htm)。CMake 锁定源码及 SHA256，只编译所需驱动/引擎；适配为替换引擎版本与 QStringBuilder 选项，没有复制业务逻辑或自制加密算法。驱动依赖 Qt 私有 API，须按相同 Kit 重编译，目前验证 Qt 6.12/MinGW。依赖源码及许可随 `licenses/Encrypted-SQLite-Source` 分发，不含业务源码。驱动上游主要提供 demo，我们补充了迁移/WAL/部署验证；EFS 不适合当前 Windows 家庭版，SQLCipher 另建加密依赖和驱动的成本更高而未引入。

2026-10-13：0.1.2 已发布，并在既有安装目录验证升级。全部表记录数及内容摘要迁移前后相同，加密备份恢复后也一致；独立安装回归验证旧聊天/草稿/附件加密、卸载保留数据及密钥。证据在 `build/security-hardening/`，未做跨 Windows 用户、其他电脑或完整云端业务实机验收。恢复工具与密码界面由 `app/securitycommands` 编排，密钥/文件权限及迁移分别在公共模块，不堆入启动函数。

## 连接本机或云端

正式版要求 `GateServer/scheme=https`、`ChatServer/tls=true`、`ResourceServer/tls=true`，拒绝关闭或省略安全设置。Debug 的回环本机服务保留 HTTP/TCP 调试能力。TLS 会校验服务器证书和主机名，聊天登录与图片请求在握手完成后发送。

云端 TLS 配置示例（`203.0.113.10` 为文档示例地址，请替换为自己的服务器域名或 IP；真实连接配置不提交）：

```ini
[GateServer]
host=203.0.113.10
port=443
scheme=https

[ChatServer]
tls=true

[ResourceServer]
host=203.0.113.10
port=8443
tls=true
```

两个聊天节点的公网 TLS 端口为 `8441` / `8442`，地址和端口由登录接口分配，不在客户端写死。切回本机时，GateServer 使用 `127.0.0.1:8090`、`scheme=http`，ChatServer 使用 `tls=false`，ResourceServer 使用 `127.0.0.1:8093`、`tls=false`。修改源码目录中的配置后，在仓库根目录执行 `CppChat.cmd sync` 并重启客户端；源码修改后还需重新编译。

Windows 发行目录需要包含 Qt 的 `tls` 插件目录；本机 Kit 使用随 Qt 部署的 Schannel 后端和 Windows 证书信任库，无需另外安装 OpenSSL。云端的服务和账号与本机独立，连接云端时直接启动客户端 EXE，或在 Qt Creator 中运行客户端。

2026-10-11：客户端已移除 qmake 工程，改用 CMake。下面较早的工具表和清理记录为历史记录。

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

开发运行时从 EXE 目录向上找到客户端 `CMakeLists.txt` 与 `app/main.cpp`，聊天数据放在本客户端根目录下：
`chat_data/accounts/<服务器哈希>/<UID>/sql/db.sqlite`，图片缓存仍在账号目录下的 `attachments/`。
开发时不使用项目外独立目录，数据不随 build 子目录变化。
发布目录没有项目文件时使用 EXE 同级的 `chat_data/`，用户安装到哪里就保存到哪里，允许 C 盘及其他盘符；只有目标不可写或真实存储操作失败时才报错。显式 `--data-dir` 可覆盖默认位置。
首次登录会兼容迁移旧 `chat.db` 与附件；已有目标数据库不会被覆盖，旧库保留。

本机现有账号随后已按用户要求直接迁移并逐表核验：22 条消息、12 个会话及同步状态完整保留。
旧 Debug 运行目录和旧 AppData 副本的数据库及 WAL/SHM 已删除，常用 Debug EXE 已更新为使用新路径的版本。

同日递归检查并将性能验证构建、日志专项构建、Python 缓存、Qt Creator 索引缓存
和旧日志移入 Windows 回收站，共 1,868 个文件、约 963 MiB。
正常客户端及资源服务构建、私人配置、聊天数据库和资源数据保留；
客户端和资源服务各保留一份最近运行日志。回收站未清空，文件仍可恢复。
