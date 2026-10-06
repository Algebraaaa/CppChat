# 客户端性能优化进度

> 2026-10-04 更新：第一、二批的“未编译”描述是当时记录。后续已获准编译，
> 当前完成情况、编译和运行验证见本文第三、四批及整理章节。

## 第一批：消息更新、图片展示和传输调度

日期：2026-10-03。对应 [性能源码审查](performance-review.md) 的优先处理项。

### 复用的开源组件和实现依据

| 来源 | 实际采用的做法 |
| --- | --- |
| [Telegram Desktop 的消息管理](https://github.com/telegramdesktop/tdesktop/blob/dev/Telegram/SourceFiles/history/history.cpp) | 按消息标识保留已有消息项。当前项目按 unique_id / server_id 复用控件，发送确认和状态变化更新现有行 |
| [Telegram Desktop 的图片视图](https://github.com/telegramdesktop/tdesktop/blob/dev/Telegram/SourceFiles/history/view/media/history_view_photo.cpp) | 区分图片数据与显示缓存，按尺寸和显示条件生成预览；当前项目把气泡缩略图与原图读取分开 |
| [Qt QThreadPool](https://doc.qt.io/qt-6/qthreadpool.html)、[QRunnable](https://doc.qt.io/qt-6/qrunnable.html) | 直接使用 Qt 的线程复用与任务执行组件，图片解码、下载、上传分别使用独立线程池 |
| [Qt QCache](https://doc.qt.io/qt-6/qcache.html) | 直接使用 Qt 按成本限制、淘汰较少使用对象的缓存，替换原有压缩字节缓存的全量计数和任意淘汰 |
| [Qt QImageReader](https://doc.qt.io/qt-6/qimagereader.html) | 在工作线程读取图片、请求缩小解码、处理 EXIF 方向；回到 GUI 线程才生成 QPixmap 和更新控件 |

Telegram 用于实现策略参考；执行、线程池和缓存直接复用 Qt 组件。没有引入 Telegram 的应用框架或复制其源码。Qt API 已对照本机 6.5.3 头文件检查。

### 已完成的源码改动

1. **消息控件增量复用**
   - `ChatPage::RenderMessages` 按稳定消息标识匹配已显示控件。
   - 新增消息只创建新增控件；正文、类型或发送者改变时替换对应行。
   - 发送确认只改变 ID、时间或状态时，正文控件继续复用。
   - 时间分隔行、查看更早记录按钮分别复用；窗口外旧行由 `ChatView::setChatItems` 删除。
   - 保留底部跟随与阅读历史时的滚动锚点。切换会话、重置会话仍清理旧控件。
   - 右键菜单从最新消息映射取对象，避免复用控件后仍对旧的待确认对象执行重试。

2. **图片后台解码与预览缓存**
   - 新增 `common/imageloader.*`，由 GUI 线程负责调度，后台只处理字节和 QImage。
   - 主聊天页和历史记录页统一调用 `PictureBubble::loadImage`。
   - 图片气泡只持有预览 QPixmap，不持有解码后的原图或每条消息一份原始字节。
   - 预览键包含登录 generation、文件 ID、目标尺寸和 DPR；同键请求合并处理。
   - 控件销毁后不再向它调用结果回调，旧账号结果不更新当前账号。
   - 查看和复制原图时才读取原始字节并后台解码，压缩字节缓存已淘汰时从附件存储或资源服务取回。
   - 原始文件字节不重新压缩；缩略图不作为查看原图或复制图片的数据源。

3. **有界传输和进度节流**
   - 下载、上传使用独立 Qt 线程池，不再为每个任务创建 QThread。
   - 账号退出时取消旧任务、清理未开始任务；活跃任务在读写等待或分块边界检查取消标识。
   - 连接建立沿用 10 秒等待，取消并不保证立刻中断正在进行的连接建立；读写等待以 100 ms 为周期检查。
   - 传输进度按约 100 ms 通知，完成时通知最终进度。
   - 失败下载残片有总预算；压缩字节采用 Qt QCache 维护。
   - 队列拒绝时返回可重试错误，不静默丢弃。原有文件校验、消息持久化与分块协议保留。

4. **重复读取和草稿工作合并**
   - 同一事件循环中的同会话回读合并；同会话已有查询时只保留一个补读请求。
   - 普通打字只标记草稿变化，500 ms 保存定时器或会话切换时才扫描文档。
   - 未变化的草稿不重复保存，发送成功后清理草稿变化标记。

### 当前默认参数

| 参数 | 默认值 | 代码位置 |
| --- | --- | --- |
| 图片解码并发 | 2 | `common/imageloader.cpp`：kDecodeConcurrency |
| 解码任务压缩字节预算，含活跃与排队任务 | 64 MiB | kPendingBytesLimit |
| 预览缓存预算 | 16 MiB，按 KiB 计成本 | kPreviewCacheKiB |
| 气泡逻辑预览边界 | 240 × 220，等比缩放 | `widgets/picturebubble.cpp`：kBubblePreviewSize |
| 图片下载并发 | 3 | `network/resourcemgr.cpp`：kDownloadConcurrency |
| 下载活跃和排队任务总数 | 64 | kMaxDownloadTasks |
| 图片上传并发 | 1 | _uploadPool.setMaxThreadCount |
| 上传活跃和排队任务总数 | 8 | kMaxUploadTasks |
| 原始压缩字节缓存 | 64 MiB | ResourceMgr 构造函数的 QCache 成本 |
| 失败下载残片预算 | 32 MiB | kPartialCacheLimit |
| 进度通知间隔 | 100 ms | kProgressIntervalMs |

这些值限制对应资源，不是整个进程内存的统一上限。打开原图窗口、剪贴板、Qt 图片解码器、其他界面和草稿仍有各自内存开销。

### 验证情况

- 已静态检查消息匹配、正文快照、右键菜单最新对象、清理窗口旧行和会话重置路径。
- 已静态检查后台结果的 receiver 生命周期、账号 generation、取消标识和 Qt 6.5.3 API。
- 已检查 `client.pri` 中 160 个清单路径全部存在，新 ImageLoader 源码和头文件各登记一次。
- 新模块和完整格式化的图片/资源模块通过 clang-format 的格式检查。这不等同 C++ 编译通过。
- 未编译、未运行客户端、未执行性能压力测试，未推送 GitHub。

编译后的重点验证：短文字/表情收发和发送确认；图片聊天的冷热切换；图片查看、复制和失败重试；带草稿切换会话；边阅读旧消息边收到新消息；图片传输中退出并重新登录。

### 第一批结束时的待办

- 大图片粘贴、PNG 编码和编辑器附件预览仍需要后台处理；本批只减少了每次打字的文档扫描。异步插入需同时处理附件顺序、撤销、切换会话和发送时机，不能简单回调后插到当前输入框。
- 会话批量排序、主搜索防抖、数据库线程与大附件 IO 分工、日志批量写入和自动设置保存合并。
- ResourceServer 文件级锁、连接上限、Redis 连接复用和传输协议吞吐优化。

## 第二批：会话调度、附件 IO、日志和设置保存

日期：2026-10-03。以下记录源码已实现的改动，运行效果仍待验证。

### 复用的组件

- **spdlog v1.15.3**：从 [官方版本](https://github.com/gabime/spdlog/releases/tag/v1.15.3) 收录未修改的头文件及许可证到 `third_party/spdlog`，实际使用 async logger、独立 thread pool、daily file sink 和 periodic worker。采用 header-only 模式，不依赖额外 DLL，升级边界和来源见该目录 README。
- **Qt QTimer**：使用单次定时器实现输入防抖、同轮事件合并和固定时间窗口保存。未重写定时器或事件队列。
- **Qt QThreadPool / QRunnable / QSaveFile**：附件 IO 使用独立、单并发线程池；文件原子替换和 SQLite 元数据登记分开处理。数据库连接仍只在原数据库线程使用。
- **Qt QSettings**：保留现有 INI 格式和账号键结构；自动变化先在内存合并，再统一同步。依据 [QSettings 文档](https://doc.qt.io/qt-6/qsettings.html)，不能只延迟显式 sync 而逐次 setValue，因为 Qt 自身也会自动同步。

### 已完成的改动

1. **会话排序合并**
   - `ConversationSection::UpdateThread` 只在时间、最后消息 ID、置顶或新增条目影响顺序时标记排序。
   - 同轮事件循环中的更新合并为一次排序；分页添加一批条目后只排序一次。
   - 登录选择第一条会话前同步应用待排序状态，置顶操作也立即应用顺序。
   - 保留原来的条目对象、当前选中对象和置顶优先规则。
   - 会话头像直接复用 `UserMgr` 已有缩略图缓存和更新信号，移除每次摘要刷新时重复加载本地头像；置顶 SVG 只在行创建时加载。
   - 文本相同、选择状态相同或红点状态相同时跳过相应的文本计算、样式刷新或显示更新。

2. **聊天与通讯录搜索防抖**
   - 输入停顿 200 ms 后才扫描并创建结果，减少逐字符删除和重建整组控件。
   - 输入变化时立即清理旧结果，等待期间不能点进旧关键词的结果。
   - 数据变化合并刷新，不打断当前输入的防抖；空搜索不扫描。
   - 聊天历史请求在防抖完成后触发一次，账号重置时停止定时器。

3. **附件 IO 与 SQLite 分工**
   - 保存：IO 线程写入 QSaveFile 并计算 SHA-256，回到 GUI 调度后在数据库线程登记元数据。
   - 读取：数据库线程查询路径、大小和校验值，IO 线程限长读取并校验字节，再返回结果。
   - 活跃及排队任务都有任务数、字节预算。旧账号任务使用取消标识；旧账号结果不调用当前账号回调，也不登记到新账号数据库。
   - 单 IO 并发避免本客户端的附件读写同时覆盖同一文件。缓存不完整或校验失败继续使用原有下载回退。
   - 发送回执可能早于缓存落盘；登记缓存时用事务清理已经确认、文件 ID 匹配的重复图片 BLOB，保留待确认消息的重试字节。
   - 新增 `messages_image_blob` 部分索引，避免上述清理每次扫描全部消息。已有数据库在下次打开时通过 CREATE INDEX IF NOT EXISTS 补建，无需重建聊天表。

4. **普通日志后台写入**
   - Qt 日志 API 与日志分类保持不变，文件继续使用 `logs/cppchat-YYYY-MM-DD.log`。
   - Debug / Info 入有界异步队列，由 spdlog 工作线程写入文件与默认控制台。
   - 队列满时采用官方 `overrun_oldest` 策略覆盖最旧的普通日志，后台记录覆盖数量；普通日志正文超过 4096 个 QString 字符时截断并标注。
   - Warning / Critical / Fatal 走共享的线程安全 sink 同步写入和刷新，不进入会被覆盖的普通日志队列。大量警告仍可能阻塞调用线程。
   - 普通文件输出每秒刷新，正常退出恢复原 Qt 处理器、排空仍在队列中的普通日志并刷新文件。若安装前有自定义 Qt 处理器，继续调用它，其执行成本由该处理器决定。
   - 日志后台异常直接报告到 stderr，避免递归进入 Qt 日志处理器。

5. **自动未读保存合并**
   - 自动未读变化先放内存，固定 500 ms 窗口后一次同步，持续来消息不会无限延后保存。
   - 同账号、同键只保留最新值；Load 能读取尚未写盘的最新状态。
   - 手动置顶、手动标为已读/未读、移除会话、账号重置和正常退出立即同步待保存设置。
   - 写入失败保留缓冲，并按 5 秒重试；每批使用新写入对象，以免旧的 QSettings 错误状态阻止成功后的恢复。

### 参数与行为边界

| 参数 | 默认值 | 位置 |
| --- | --- | --- |
| 搜索输入防抖 | 200 ms | `dialogs/conversationsection.cpp`、`dialogs/contactssection.cpp` |
| 普通会话排序合并 | 同轮事件循环，单次 0 ms 定时器 | `ConversationSection` |
| 自动未读保存窗口 | 500 ms | `common/conversationpreferences.cpp` |
| 设置保存失败重试 | 5 秒 | `PreferenceStore::Flush` |
| 附件 IO 并发 | 1 | `ChatStore` 构造函数 |
| 附件活跃与排队任务总数 | 32 | kMaxAttachmentTasks |
| 附件任务字节预算 | 64 MiB | kMaxAttachmentBytes |
| 单个缓存附件 | 最大 20 MiB，沿用原来的读缓存限制 | kMaxAttachmentSize |
| 普通日志队列容量 | 2048 条 | `common/logger.cpp`：kLogQueueCapacity |
| 普通日志正文上限 | 4096 个 QString 字符 | kMaxRoutineMessageCharacters |
| 普通日志文件刷新 | 1 秒 | `LogBackend::StartFlushing` |

新增后台执行资源是一个按需工作的附件 IO 线程，以及 spdlog 的一个日志消费线程和一个周期刷新线程。GUI 控件仍由主线程操作。普通 TCP 和 HTTP 仍通过 Qt 事件循环运行。

附件预算包含请求保留的字节或预期读取量，旧账号任务直到完成结果交接才释放预算；不代表进程总内存上限。缓存队列满时，读缓存会回退下载；写缓存通过回调返回错误，不影响已经完成的服务端图片传输。缓存写入未登记元数据时退出账号或程序，可能留下无索引缓存文件，后续重新下载即可。消息数据库已排队的写入仍按原来的关闭流程处理。

500 ms 自动设置窗口与普通日志缓存属于性能取舍：异常终止时可能丢失尚未保存的自动未读变化或普通日志。手动设置与严重日志即时同步仍可能产生短暂 IO 等待。未改动消息事务、消息确认与重试协议。

### 验证与剩余工作

- 已运行 `scripts/test_chatstore_sql.py`，11 个用例通过；新增用例验证“缓存晚于回执完成”的清理只影响已确认、匹配文件的图片，保留待发送图片，并检查清理查询使用索引。
- SQL 测试使用 Python SQLite 与临时数据，不启动 Qt 或真实账号；不证明跨线程调度已经运行正确。
- 已核对 vendored spdlog 的 105 个头文件与官方 v1.15.3 压缩包逐字节一致，许可证和 fmt 11.2.0 版本已检查；`client.pri` 中 164 个字面清单路径全部存在。
- 已格式检查本批修改的完整模块，已静态检查定时器重置、初始会话排序、generation、任务预算、退出及日志队列策略。
- 未编译、未启动客户端、未测量耗时或帧率、未推送 GitHub。

编译后重点验证：登录即显示并按时间排序；批量收消息时排序与红点；连续输入、清空搜索以及搜索期间收到消息；手动与自动已读后退出重登；传图片时同时查询历史；缓存读写中切换账号；正常退出时最后一批日志写入。

后续优先项仍包括大图片粘贴/PNG 编码及附件草稿的异步插入，以及 ResourceServer 文件锁、连接上限和 Redis 连接复用。主聊天 TCP JSON 解析也需以实际压力数据判断是否值得拆到后台。

## 第三批：输入附件、TCP 调度和表情面板

完成日期：2026-10-04。本节及后续记录取代前文当时的待办状态。

### 输入附件后台处理

- 新增 `common/attachmentpreparer.*`，复用 Qt QThreadPool / QRunnable；并发 2、任务上限 16、输入保留量预算 128 MiB。
- 文件读取、PNG 编码和预览缩放进入工作线程。原文件上传字节保持不变；粘贴和截屏的 QImage 在后台编码 PNG。
- 沿用单附件 20 MiB 上限，增加 16 Mi 像素检查。PNG 输出通过有界 QBuffer 边写边限制，超限不保留半张图片。
- GUI 当场在光标处插入带唯一 ID 的图片占位，完成后只更新 document resource；不重新插入图片，也不增加撤销步骤。
- MsgInfo 持有仅在 GUI 线程更新的共享准备状态，待完成附件随原草稿保存；回调不会插入新会话的编辑框。
- 未完成或失败的附件阻止整份草稿发送。删除附件后不再阻止发送；失败附件不作为空图片写入草稿，其他成功内容保留。
- 切换会话不等待工作线程。关闭账号时等待已接受的准备任务、交接结果并提交旧账号草稿，然后才关闭数据库；不调用 processEvents。
- 草稿 SQL 改为按 ordinal 条件 UPSERT，删除超出当前长度的尾部。未变化的图片行跳过写入，避免每次文字修改都删除再写整份附件 BLOB。

预算限制请求保留量，不等同整个进程内存上限。图片格式不同，解码器实际中间内存不同。
正常退出可能等待图片编码和数据库写入；异常终止仍可能丢失尚未完成的草稿。

### TCP 突发处理

- 接收缓冲上限 1 MiB，每轮最多读 256 KiB、处理 32 帧或使用约 4 ms，然后通过单次 QTimer 继续。
- 同批只 remove 一次已消费的缓冲前缀，保留半包；请求处理、JSON 解析和 GUI 信号仍在主线程，保持顺序。
- 阻止嵌套事件循环重入解析。连接 generation 变化后丢弃旧连接剩余帧；若嵌套循环已经接收到新连接数据，继续调度新数据。
- 单条 JSON 解析和信号回调不能被 4 ms 预算中断；这个值是批次预算，不是绝对 UI 延迟保证。

### 表情面板

- 搜索防抖 150 ms，输入变化时隐藏旧结果，防止误点旧关键词结果。
- 按目录 ID 复用 QToolButton 和 QIcon，只重新安排小型布局容器。最近使用与主列表分别保留实例，收藏操作继续使用既有持久化规则。
- 缓存实例数量受有限表情目录约束；不创建控件后台线程，也不增加新的 UI 框架。

## 第四批：ResourceServer 和历史查询

### 服务端改动审查与整合

保留并审查另一个任务加入的异步 Asio 会话和共享 BusinessExecutor，不覆盖该实现。
资源业务仍从后端 ChatServerShared 编译，独立打包时必须通过 CPPCHAT_SHARED_SERVER_DIR 指定路径。
共享源不存在时，CMake 提供明确错误；Boost 使用 CONFIG 包查找。

- 异步连接和超时留在单个 I/O 循环，文件、JSON、Redis 工作交给有界业务线程队列。
- 默认业务线程 4、排队上限 256、连接上限 128、操作/空闲超时 30 秒，分别由现有环境变量配置。
- 保留 64 把文件分片锁。相同文件及 Windows 大小写别名串行；不同文件通常可并发，哈希冲突仍会串行。
- 按 [hiredis 官方线程安全要求](https://github.com/redis/hiredis)，每个业务线程使用独立 thread_local context；断线后的下一次使用重新连接并 AUTH，不在线程间共享 context。
- 每个请求继续执行 TTL/GET 鉴权，不缓存 token 验证结果。连接与命令超时均为 2 秒；故障当次请求明确失败，不自动绕过鉴权。
- UploadInit 增加 upload_window=4。新客户端保持 32 KiB 分块，最多先发 4 块再依序读取确认，严格核对每块偏移；旧服务端没有字段时回退窗口 1。
- 失败时关闭传输 socket，重新初始化读取服务端真实断点；不在旧 socket 上继续读可能积压的 ACK。上传完成必须得到 complete 确认。
- 校验失败删除坏 .part，允许重新上传；完整 .part 在改名前退出的场景可在再次初始化时校验并恢复。

这不是新的二进制协议：16 位帧长、JSON/Base64、权限和 SHA-256 校验保留。
下载目前仍逐块交换；上传窗口改善等待方式，不保证所有网络环境固定倍数加速。

### 历史查询

采用 [SQLite 官方滚动窗口查询](https://www.sqlite.org/rowvalue.html)和
[表达式索引](https://www.sqlite.org/expridx.html)的做法：

- 新增 messages_history_order，按 thread_id、COALESCE(server_id)、local_id 排序；已有库打开时补建索引，不改消息表或重建数据。
- 历史窗口维护已访问页面的游标，下一页依据上一页最后一条记录定位，替换深页 OFFSET；未确认消息仍按原规则排在最前面。
- 日期筛选使用闭开时间范围，移除逐行 substr；中文、百分号、下划线的字面量搜索规则保留。
- 排队期间被新搜索取代的查询在执行前跳过。已经执行的查询仍在 SQLite 线程正常结束，过期结果不更新界面。
- EXPLAIN 已确认深页查询使用表达式范围索引，并无临时排序 B-tree。

LIKE 前置通配符和链接筛选仍可能扫描会话内不匹配记录。本轮保留子串搜索语义，未引入
会改变中文搜索结果的分词器。索引增加磁盘空间、首次补建和消息写入成本。

## 编译、运行验证与最终整理

本机工具版本见根目录 [README](../README.md)。已在独立 build/performance-validation 目录构建：

- CppChat：Qt 6.5.3 / MinGW GCC 11.2.0，Debug，成功。
- performance_tests：直接通过 client.pri 编译当前客户端源码，成功。
- ResourceServer：VS 2022 / vcpkg x64-windows，Debug，成功；保留并链接共享业务执行器。
- 最终客户端和测试构建日志未检出 warning/error；spdlog 使用系统头文件搜索路径，未修改上游源码或关闭项目警告。

运行结果：

| 验证入口 | 实际结果 |
| --- | --- |
| tests/performance.pro，offscreen Qt 全套 | 12 passed、0 failed、1 skipped；单独网络用例由下项提供隔离服务后执行 |
| tests/resource_protocol.py --client | Qt 资源用例 3 passed、0 failed；真实客户端资源模块上传及重新下载 1,048,613 字节完全相同 |
| 真实 ResourceServer + RESP/TTL fixture | 窗口 ACK、原始字节、权限、token 更换/过期、断点、坏校验重试、完整 .part 恢复、Redis 重连、6 文件并发、连接上限和空闲超时通过 |
| Redis 连接观察 | 初始 71 次命令只使用 2 条连接，对应测试设置的 2 个业务线程 |
| scripts/test_chatstore_sql.py | 13 个测试全部通过，包含游标翻页、索引计划和不重写图片草稿行 |
| tests/history_query_benchmark.py | 10 万行、深度 9 万，两种方式均使用当前索引且结果相同；10 次中位数：游标约 0.197 ms，OFFSET 约 32.296 ms |
| scripts/check_style.py | 12 模块、49 参数、20 控件角色和资源路径检查通过 |

Qt 回归覆盖图片插入位置与光标、完成前后撤销/重做、重复图片片段、剪贴板粘贴、原文件字节、
失败图片删除、任务预算释放、超大图片有界编码和事件循环继续运行、非当前会话草稿落库、
退出重登草稿、表情按钮复用、TCP 半包/突发让出事件循环/断线丢弃/嵌套循环重连。
超限 PNG 用例触发 libpng 的预期 Write Error，并检查失败结果不保留编码字节。

上述耗时来自合成 SQLite 测试，不是 GUI 帧率或真实服务吞吐；协议验证使用临时 RESP/TTL
模拟器，不执行 Lua，不等同真实 Redis/MySQL 与多客户端完整联调。未改真实账号数据库、重启运行中的服务或推送 GitHub。

整理结果：移除 5 个未被 CMake 使用的重复服务端拆分草稿（settings、redisconnection、resourcestore）；
移除无人调用的 getMsgList、未使用的 MsgInfo.pixmap 与编辑器无用头文件；新增模块纳入
client.pri，验证工程/脚本纳入 CppChat.pro，统一本轮 C++ 格式。既有 README 内容保留。
检查后现有客户端头文件均有外部引用，没有继续删除在用控件、动态头像资源、上游许可证、
私人配置、构建产物、运行数据或其他任务的备份。

### 复现入口

2026-10-06：按用户要求删除客户端 `scripts/` 和 `tests/`，并清理工程登记。
同日将 `build/performance-validation/`、`resource_server/build-logging/`、
遗留 Python 缓存和旧编译/运行日志移入 Windows 回收站，原测试程序路径不再保留。
本节命令及上面的测试路径仅作为历史验证记录，不再是当前源码的可运行入口。
保留第三方 spdlog、客户端业务代码、资源服务以及已记录的验证结果。

在 MinGW Kit 的 PATH 中使用 qmake/mingw32-make，从独立目录构建 tests/performance.pro；
运行时配置 Qt 插件目录，使用 QT_QPA_PLATFORM=offscreen。

```powershell
python scripts/check_style.py
python scripts/test_chatstore_sql.py
python tests/history_query_benchmark.py
& ./build/performance-validation/tests/debug/performance_tests.exe -o results.txt,txt
python tests/resource_protocol.py --resource ./build/performance-validation/resource-server/Debug/CppChatResourceServer.exe --client ./build/performance-validation/tests/debug/performance_tests.exe
```

资源协议脚本只接受独立验证目录下的客户端测试程序，并拒绝替换已有 config.ini；
fixture 结束后删除自己创建的配置和临时资源目录。
