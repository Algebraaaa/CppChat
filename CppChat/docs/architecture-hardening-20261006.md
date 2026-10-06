# 架构加固进度（2026-10-06）

清理说明：四阶段完成后按用户要求删除测试源码/脚本、架构检查脚本、临时验证构建及日志（包括 `.validation`）。下文命令、目录和结果仅保留为历史记录，当前源码不再包含这些自动验证入口，也不再保留对应产物。正常业务工程及启动/协议生成脚本不受影响。

保留现有 Qt Widgets 客户端、Boost.Asio/gRPC 服务端技术栈，保留两个独立聊天节点工程和唯一公共源码。按上一轮审查的风险顺序处理，不再围绕单个接口制造碎片类。

## 顺序

1. 连接线程归属与网关阻塞隔离：已完成，证据见下文第一步。
2. 生命周期：已实施停止接收、排空业务及网络写、限时关闭（包括踢下线）、断线 Redis 清理移出 I/O、异常启动收尾；正常停机测试与网关连接准入上限已补齐，验证记录见第二步。
3. 客户端：已按完整消息发送和历史同步流程，将业务编排从 ChatPage/ChatDialog 分离；保留 .ui 页面边界，验证见第三步。
4. 边界整理：已完成会话服务、DAO 领域实现、资源服务入口和可协商的请求关联契约；保留简单、可阅读的接口，验证见第四步。

## 第一步改动

- 共享 `CSession` 增加连接 strand；Start、async_read、async_write、shutdown/close 统一在它上面执行。对外 GetSocket 只用于尚未 Start 的独占 accept 阶段。
- 业务线程先在有界发送队列中准入，再调度首次写；不把每条消息先 post 到无界 I/O 队列。异步写持有消息缓冲区，关闭清空队列后缓冲区仍然有效。
- 踢下线通知和关闭标记一起准入，标记之后不再接受普通发送，业务分发视为已关闭并跳过排队请求。慢客户端积压满时断开连接，避免静默丢包。
- 主停机流程不再在 signal 回调中抢先停止连接 I/O；关闭操作在所属执行器执行完成后，才停止 I/O 池。尚未实现“所有发送队列排空后停机”的承诺。
- GateServer 复用共享 `BusinessExecutor`，请求在读完后交业务线程，业务完成后 post 回连接 strand 发包。路由逻辑仍集中在原 LogicSystem，本次未改正常业务 JSON 字段。
- 网关业务默认 4 个分片、256 个排队任务；满时返回 503，响应 JSON 的 error 为 HTTP 状态值。它是传输保护，不是新增业务 ErrorCodes。
- 网关超时覆盖读请求、排队、业务等待、写回；排队时已超时则跳过业务。已开始的数据库/RPC 不能因此保证取消或回滚。
- 网关头部 8 KiB、正文 32 KiB；非法百分号转义返回 400，不再触发 Debug 断言或 Release 越界读取；不支持的方法返回 405。
- Release 回归发现“拒绝超大正文后立即关闭导致 RST、客户端丢失 413”的收尾竞争。发送完成后先 shutdown_send，再用最多 1 秒、64 KiB 的异步 drain 收尾；不在 I/O 回调中阻塞等待，也不无限读取恶意正文。

## 配置与工程

不修改实际 config.ini。网关可选配置如下；未配置则使用这些默认值：

```ini
[Business]
Workers=4
QueueLimit=256
[GateServer]
RequestTimeoutSeconds=60
```

Workers 范围 1-16、QueueLimit 范围 1-65536、RequestTimeoutSeconds 范围 1-300。排队数不包含正在执行的任务。
分片键在连接建立后不变化；一个慢请求仍可能阻挡同分片业务，但不会阻挡对应 socket 的 I/O 回调和超时。
网关 .vcxproj 和 .filters 都列入公共执行器，没有复制第二份源码。

构建产物和日志位于 `build/backend-threading-validation`（Git 忽略），不创建源码备份目录，不恢复 `.migration-backup` 或 `.validation`。
完整后端验证脚本的默认产物目录同步改为 `build/backend-validation`，新增可选 `--gate` 隔离测试。
本阶段可单独重复验证（只构建两个聊天节点、网关和执行器测试）：

```powershell
powershell -NoProfile -File E:/Project/VStemp/ChatServerShared/tests/validate_backend.ps1 -ThreadingOnly
```

若已具备本阶段最新产物，加 `-SkipBuild` 只执行静态检查、CTest 与隔离网络测试；默认输出为 `build/backend-threading-validation`。完整五服务验证仍可不带 `-ThreadingOnly` 执行。

## 验证与边界

本节保留第一步完成时的证据与限制；第二步新增的正常停机证据见文末。

最终源码使用本机 VS 2022 / MSVC v143 编译链接：

| 工程 | x64 Debug | x64 Release |
|---|---|---|
| ChatServer | 通过 | 通过 |
| ChatServer2 | 通过 | 通过 |
| GateServer | 通过 | 通过 |

执行器 CTest 在 Debug/Release 均通过。最终执行 `validate_backend.ps1 -ThreadingOnly -SkipBuild`，两种配置的静态检查、两个节点的断开压力测试与网关回归均通过，输出 `THREADING_VALIDATION_PASSED`。
本轮没有重新构建未涉及的 StatusServer、ResourceServer 或 Qt 客户端；此前五服务结果仍见原验证记录。
编译日志位于 `build/backend-threading-validation/*-Debug.log`、`*-Release.log`，依赖头文件仍有既有第三方警告，不声称零警告。

测试使用脚本启动的实际 EXE、随机回环端口、内存 RESP/TTL Redis fixture，以及故意不返回 HTTP/2 握手的 RPC fixture。
网关 MySQL 指向随机的未监听回环端口，只测试不依赖 SQL 的路由；不连接开发数据库。

- 聊天节点：24 个并发连接，600 次无效登录回包、24 次未登录查询拒绝；另外 192 次 EOF/RST/非法下一帧头竞争，压力之后仍能正常回包。
- 网关：正常 GET、错误 URL 转义、未知路由、不支持方法、无效 JSON、超大正文分别验证 200/400/404/405/200/413。
- 超大正文 413 连续回归 32 次，并验证只发送超大 Content-Length 而不发送正文时也及时拒绝。
- 网关业务线程被 RPC fixture 阻塞时，队列满响应 503；两个等待中的 HTTP 连接按时关闭；放开 RPC 后，过期排队请求不执行第二次 RPC，服务恢复正常 GET。
- 现有执行器 CTest 继续覆盖有界准入、分片 FIFO、异常存活和 Stop 排空，不把它当作 socket 生命周期测试。

尚未验证真实 Redis Lua、MySQL 事务、成功用户登录与跨节点踢下线、最终消息送达、正常停机排空或真实压测；本轮网络测试的结束是终止测试进程，不是优雅停机证明。
没有启动/重启开发中的正式服务，没有修改数据库、实际配置或客户端已有未提交的 UI 修改；未提交、未推送 GitHub。

## 第二步：生命周期与停机

本阶段沿用两个聊天节点工程与唯一公共源码，不更换技术栈，不调整客户端页面。

退出流程由服务入口持有者统一负责：停止监听与业务准入 → 等待已接纳业务 → 等待发送/物理关闭 → 等待凭证清理 → 停止 I/O → 销毁 RPC/数据库等依赖。
聊天节点还会停止 gRPC 接纳、等待维护线程并撤销自己拥有的健康租约；撤销操作继续比较 instance_id。
取消中的 accept 回调通过主事件循环先处理完，避免它持有的工作 socket 活得比 I/O 线程池更久。

- `BusinessExecutor` 增加非阻塞 `BeginStop` 与带期限的 `WaitIdleUntil`。`Stop(false)` 只丢弃等待任务，必须等待正在执行的任务结束；业务主队列仍使用排空模式。
- `CSession::DrainAndClose` 在所属 strand 上封住新的发送准入，排空现有发送队列，最多等 5 秒后物理关闭。关闭完成通知返回后才停 I/O 池。
- 踢下线的末尾通知仍与关闭标记原子准入，增加 3 秒期限。旧连接留在 CServer 中直至物理关闭/清理完成，同服重登录只立即解绑旧 UID 映射，不提前移除正在排空的 socket。
- 断线清理由物理关闭统一触发，删除多处重复手动清理调用。I/O 线程只解绑本地映射并提交清理任务，不同步等待 Redis。
- 凭证清理由 2 个独立工作线程执行；关闭会话保留在连接表中到清理结束，因此原连接准入上限也限制待清理积压。
- 停机等待清理最多 5 秒，之后丢弃尚未执行的清理；已开始的 Redis 操作仍按其命令/借用/连接超时安全收尾。异常/未执行的撤销依赖凭证 TTL，并明确记录日志。
- 心跳维护扫描在每条会话前检查停止标记，停机不再继续逐个续租剩余会话。维护线程结束并撤销健康租约后，才等待 gRPC 收尾，降低新分配指向已停接纳节点的窗口。
- 网关保存活动连接表，默认上限 1024，可配置 `[Business] MaxConnections`（1-100000）。空闲和半包连接停机时关闭，已接纳业务继续按原请求超时和额外写排空期限收尾。
- HTTP 发送后的 FIN/输入 drain 不延长已有期限，仍保留最多 1 秒、64 KiB 的收尾边界。
- 服务入口持有者覆盖部分启动失败；I/O 线程池构造中途创建线程失败时，也会先停止并 join 已启动线程，避免 joinable 线程析构终止进程。

### 不能混淆的边界

5 秒是连接写排空/清理等待的期限，**不是整个进程必在 5 秒内退出**。已经执行的 SQL 尚无命令级超时，不能安全强杀其线程；等待其结束可能延长退出。
Redis 活动凭证最长 1 小时。清理失败或排队任务被丢弃时，凭证可能保留到 TTL 到期，不能宣称即时撤销。
发送完成不等于接收方业务确认；断网/慢客户端可能在超时关闭前未收到消息。

### 第二步验证

复用 `tests/validate_backend.ps1`，在 `build/backend-threading-validation` 构建全部五个后端工程，额外运行 `tests/lifecycle_smoke.py`。
本阶段实际执行的完整命令如下（已有最新构建产物时可加 `-SkipBuild`）：

```powershell
powershell -NoProfile -File E:/Project/VStemp/ChatServerShared/tests/validate_backend.ps1 -BuildRoot E:/Project/Qtemp/myPractice/CppChat/build/backend-threading-validation
```

最终源码的 ChatServer、ChatServer2、GateServer、StatusServer、ResourceServer 均完成 x64 Debug/Release 构建；两种配置的执行器 CTest、原 TCP 回归与新增生命周期回归全部通过，完整脚本输出 `BACKEND_VALIDATION_PASSED`。
最后补充维护扫描停止标记与提前撤销健康租约后，已重新编译两节点，并再次执行上述完整脚本，不使用修改前的产物作为最终证据。
最终正常停机日志在 `build/backend-threading-validation/lifecycle-om_lpngw`（Debug）与 `lifecycle-zg_ope63`（Release）；编译日志仍在同一 build 根目录。依赖头文件有既有警告，不声称零警告。
资源服务复用了有变动的公共执行器，因此本阶段一并重新构建并回归；StatusServer 也做了完整构建检查。

生命周期测试只控制自己创建的真实 EXE，使用随机端口、RESP Redis fixture、故意阻塞 HTTP/2 握手的 RPC fixture，MySQL 指向未监听的随机端口。
Windows 的 Ctrl+C helper 附着测试子进程的隐藏控制台，[信号只发给该控制台](https://learn.microsoft.com/en-us/windows/console/generateconsolectrlevent)；失败兜底 terminate 不计为正常停机证据。

- 每个聊天节点收到正常退出信号后，测试端仍读到 192 条无效登录回包，随后收到 EOF，进程退出码 0，自己的健康租约被移除。
- 每个节点覆盖独占端口占用、不同 instance_id 的同名健康键冲突；进程非零退出但完成清理，不删除其他节点的健康键。
- 网关先验证活动连接上限拒绝超额空闲连接，再在一个 RPC 请求正在执行、GET 已排队时发送正常退出信号。新连接拒绝，空闲连接关闭，两个已接纳请求返回响应后退出码 0。
- 网关也覆盖工作线程已经启动之后的监听端口构造失败，确认入口走完依赖清理。
- 执行器新增停止准入、等待空闲超时、只丢弃排队任务、等待活动任务结束和重复 Stop 测试。

测试仍不执行真实 Redis Lua 或 MySQL。成功用户断线凭证清理、真实跨节点踢下线、慢消费者触发写排空超时、停机时 Redis 阻塞的维护扫描等分支，属于后续部署回归，不把本次无效登录测试当作它们的端到端证明。
没有新增不再使用的生产源码文件，不创建源码备份目录，不删除运行数据或其他任务的文件；客户端现有未提交修改保持原样。

## 第三步：客户端完整流程分离

新增 `services/`，只按两条完整流程划分，职责和生命周期见 [流程服务说明](../services/README.md)。

- `MessageSender` 从 ChatPage 接管校验/文本分段、UUID 与发送前落盘、混合图文队列、图片上传后的文件 ID 落盘、TCP 发送、失败状态、同 UUID 手动重试和 15 秒确认超时。
- `ChatSyncService` 从 ChatDialog 接管账号存储初始化、本机会话/游标恢复、服务端会话分页、历史队列与整页校验、消息与游标事务提交、实时通知/回执合并和本地补读合并；保留每分钟核对历史。
- 页面只提交这些流程操作、接收按会话标识的结果并刷新控件；草稿编辑、气泡渲染、查找记录窗口、四模块导航和未读/置顶展示仍属于 UI。
- 不复制 ChatStore、ResourceMgr、TcpMgr 或 UserMgr；新服务不是单例，在 GUI 线程编排已有异步基础设施，由页面的 QObject 父子关系管理生命期。
- 发送落盘成功后才清空对应会话的编辑内容；图片和文字保持队列顺序。回执沿用 UUID 合并，实时最大 ID 不推进历史连续游标，历史页和游标同事务成功后才请求下一页。
- 账号 Reset 增加流程版本；本地读取/实时合并还核对会话对象及删除版本。旧账号/已删除会话的回调不能刷新新状态。重试采用独立发送批次，旧超时不能误标新批次，批次记录到期回收。

从旧页面删除已迁移的方法、连接和状态成员，没有留下第二份业务实现。`client.pri` 登记新源码、头文件和说明，按模块排序；本轮没有废弃的独立生产文件需要删除。
原有 UI、资源、配置、协议字段及其他任务的客户端改动保留。没有更改后端源码、运行数据或正式服务；未提交、未推送 Git。

### 第三步验证

使用本机 Qt 6.5.3 / MinGW GCC 11.2.0 / C++17，最终客户端 Debug 和 Release 均编译链接通过。
测试链接同配置的最终生产对象，不复制业务实现；两种配置各有 13 项业务用例通过，QtTest 汇总各为 15 passed、0 failed（包含初始化和清理）。

覆盖：

1. 中文、UTF-16 代理对和已识别表情标记的分段完整性及保守 JSON 预算。
2. 文本落盘早于 TCP 发包；确认按 UUID 合并到同一行，核销待确认记录。
3. 发送前 SQLite 写入失败：不发包、不清空草稿、不产生半条消息。
4. 文字→图片→文字顺序；图片上传完成、文件 ID 保存后才发元数据。
5. 图片上传失败保留本机字节，手动重试沿用同 UUID，重复点击不重复排入正在上传的消息。
6. 实时重复消息不重复发新增未读信号，通知不会跳过历史同步游标。
7. 原发送的 15 秒期限已过而重试期限未到时，新批次仍保持发送中，没有自动重发。
8. 两页历史：上一页游标已经提交后才请求下一页。
9. 历史返回游标不匹配：不落盘、不推进游标。
10. 历史页第二条写入失败：整页回滚，游标不提交、不请求下一页。
11. Reset 发生在异步保存回调前：不发送旧消息、不发旧刷新结果。
12. 删除后重建同 ID 会话：旧消息合并回调不能写入新内存会话或触发新增未读。
13. 实际 ChatDialog/ChatPage 的编辑器、发送按钮、发送后清空和四侧栏切换；发送期间切到收藏，后台结果不抢回聊天页。

验证源文件、测试工程、构建产物和日志仅在 Git 忽略的 `build/client-workflow-validation/`，
没有重新创建用户已删除的客户端 `scripts/` 或 `tests/`。完整构建日志为 `debug-build.log`、`release-build.log` 与最终增量 `final-build.log`；测试日志为 `debug-tests.txt`、`release-tests.txt`。
QtTest 临时 EXE 在独立随机 E 盘目录运行，回环端口上的聊天/资源 fixture 配合真实 SQLite；Qt 的标准路径开启 test mode，避免控件读取正式会话偏好。测试结束将自己创建的临时运行目录收纳到 build 内，保留验证证据。不修改真实账号数据、开发 MySQL/Redis 或正式聊天服务。

UI 用例使用 offscreen 插件，是控件行为回归，不是人工视觉验收；该插件报告字体目录/窗口能力警告，不把它们作为界面已视觉验收或零警告的证据。
此次没有验证真实双客户端、跨节点消息送达或服务端去重事务；客户端隔离回归不能替代这些部署验收。
旧事务或网络请求已经提交后，Reset/删除只隔离后续运行状态，不保证撤销已执行操作。第三步当时没有通用 request_id；第四步新增可协商关联，旧服务端兼容路径仍有迟到回包歧义。

第三步之后按原顺序整理登录服务、DAO 领域边界、资源服务入口和协议契约，结果见第四步，不继续拆细 UI 页面。

## 第四步：会话、数据、资源与协议边界

原四步计划的代码整理已完成；不将其等同于生产部署验收。

- `SessionService` 接管登录/心跳完整业务。`LogicSystem` 保留认证门禁、JSON 参数校验、分发和回包；登录锁、旧会话处理、Redis 注册/撤销和本地绑定顺序沿用原实现，没有每个接口一个类。
- `MysqlDao` 保持原公开接口，SQL 实现分别位于 `MysqlDaoUsers/Contacts/Messages.cpp`；主文件仅保留配置/池生命周期，`MySqlPool` 独立管理连接，内部 `MysqlDaoSupport` 共用 RAII 连接守卫、事务及资料读取。各领域不新建独立池或 Repository 转发层。
- 接受好友、建立私聊与初始消息继续在原连接/原事务中执行，删除好友留在好友领域。迁移前后 24 个方法（包括构造、析构和重载）的函数体逐一做去格式/注释 token 比较，全部一致，没有改 SQL 和表结构。
- 资源服务 `main.cpp` 缩为入口；`ResourceConfig` 管环境默认值与边界，`ResourceStore` 管 token/TTL、文件与元数据，`ResourceServerApp` 管异步连接/监听、受限工作线程和启动停止，`ResourceProtocol` 集中原消息编号、限制与错误包络。
- `TcpMgr` 对已有受跟踪请求每次尝试生成新的 `request_id`，先核对回包再停止超时/分发。服务端正常/错误回包回传合法标识，登录声明 `request_correlation=true`；正文预算包含新增字段。历史分页计入关联包络，心跳快照追加包络后再检查上限。
- 文本仍用 `unique_id` 去重/确认；通知、心跳快照、头像和资源连接保留原关联边界。旧服务端未声明能力时保留原兼容路径，不声称能够区分所有同目标重试。契约和限制见 [协议说明](protocol-contract.md)。

两个聊天节点仍是两个独立 VS 工程，通过唯一 `ChatServerShared.props` 引用公共源码；两个 `.filters` 同步显示新领域实现。没有合并节点配置、进程或在线状态。旧 `MysqlDao.cpp`、资源 `main.cpp` 仍被构建使用，并非废弃文件；迁出的实现已从原文件删除，没有留下重复生产实现。

### 第四步验证

最终源码完成 Qt 6.5.3 / MinGW 客户端 Debug、Release 构建，以及 MSVC x64 的 ChatServer、ChatServer2、GateServer、StatusServer、ResourceServer Debug、Release 构建。

后端运行完整 `tests/validate_backend.ps1 -BuildRoot E:/Project/Qtemp/myPractice/CppChat/build/backend-threading-validation`，修正分页包络预算后重新构建并复验，输出 `BACKEND_VALIDATION_PASSED`。两种配置的静态检查、执行器 CTest、TCP/HTTP/资源回归和正常退出/异常启动生命周期测试均通过；日志为 `build/backend-threading-validation/phase4-final-validation.log`，各工程编译日志也在此目录。依赖既有警告未被隐藏，不声称零警告。

两个实际聊天节点新增协议回归：参数错误回传不同尝试的标识；64 字节合法边界；未登录历史请求的认证错误也回传标识；null/数字/空串/65 字节标识以及非对象 JSON 安全拒绝。

客户端测试链接各自 Debug/Release 的生产对象，每种配置 15 项业务用例均通过，QtTest 汇总各为 **17 passed、0 failed**（包含初始化/清理）。保留第三步 13 项回归，增加：

1. 同一会话取消历史请求再重发：旧成功包、旧错误包以及严格模式下缺少标识的包不能消费新槽位；新标识的有效响应正常分发。
2. 服务端未声明关联能力、不回传标识时，重新登录和历史加载走兼容路径。

客户端最终构建日志为 `build/client-workflow-validation/phase4-build.log`、`phase4-test-build.log`；测试日志为 `phase4-debug-tests.txt`、`phase4-release-tests.txt`。测试 EXE 在随机的独立 E 盘目录运行，配合回环聊天/资源 fixture、真实 SQLite 和 Qt test-mode 标准路径，结束后将自己创建的目录收纳到忽略的 build 内。没有重新创建用户已删除的客户端 `scripts/` 或 `tests/`。

后端 fixture 不连接真实 MySQL/Redis，不证明 SQL 执行、真实 Lua、成功登录、双客户端跨节点送达或服务端去重事务。offscreen 控件测试不是视觉验收。协议关联不取消已经执行的业务，资源文件重组也没有新增停机送达保证；登录初始好友列表整体正文预算、SQL 命令超时、TLS 和持久通知补偿仍未解决。

未修改实际 config.ini、正式数据或现有服务进程，没有恢复 `.migration-backup`/`.validation`，未提交或推送 Git。后续应在明确的测试账号、数据库和两个节点部署环境中做集成验收，不为继续“拆文件”再增加抽象层。
