# 性能源码审查

后续实现进度见 [客户端性能优化进度](performance-improvements.md)。下面保留首次审查时的源码结论；已整改项以进度文档为准。

审查日期：2026-10-03。

2026-10-12 路径更新：本文中的 `resource_server` 是首次审查时位于客户端内的图片后端，现已移至仓库根目录 `ResourceServer/`。下面保留历史审查结论和当时路径。

范围：当前仓库的 Qt Widgets 客户端及 `resource_server`。外部目录中的 GateServer、ChatServer、StatusServer、VerifyServer 不属于本次已检查的源码范围。

证据级别：源码审查，未编译、未运行压力测试、未测量帧率或耗时。下面区分“代码确定执行的工作”和“这些工作可能产生的性能影响”，优先级是整改建议，不是实测排名。本次只增加本报告，不修改业务代码。

## 1. 客户端已经使用多线程

| 执行位置 | 当前职责 | 代码入口 |
| --- | --- | --- |
| GUI 主线程 | 界面、聊天 TCP 回调和 JSON 解析、HTTP 回调、消息气泡创建、部分图片解码与草稿整理 | `app/main.cpp`、`network/tcpmgr.cpp`、`dialogs/chatpage.cpp` |
| SQLite 工作线程 | 数据库打开、消息事务、查询、草稿，以及附件文件读写和校验 | `common/chatstore.cpp`：构造函数、`Execute` |
| 图片传输工作线程 | 每个上传/下载任务创建 QThread，在任务内创建 QTcpSocket，传输文件、分块、校验 | `network/resourcemgr.cpp`：`uploadImage`、`downloadImage` |
| ResourceServer 连接线程 | 独立服务进程，每个连接创建一个 std::thread | `resource_server/main.cpp`：`main`、`ServeClient` |

因此，当前准确表述是：**客户端采用 GUI 主线程、数据库工作线程和图片传输工作线程，普通聊天网络仍通过 GUI 线程的事件循环异步处理。**

ResourceMgr 的 `waitForConnected`、`waitForReadyRead` 等等待发生在传输工作线程，不能据此判断 GUI 被网络等待阻塞。异步网络也不代表收到数据后的 JSON 解析和界面更新自动进入后台线程。

## 2. 优先处理

### P1：新增消息仍可能重建最近一页聊天控件

入口：`dialogs/chatdialog.cpp` 的 `ReceiveMessages`、`ReceiveHistory`、`LoadLocalMessages`；`dialogs/chatpage.cpp` 的 `SetChatData`。

确定的执行链：保存消息完成 → 读取该会话最近 50 条 → 更新内存消息 → 当前会话调用 `SetChatData`。

当前已有内容签名比较：内容和顺序不变时，复用气泡并只更新状态。**新增消息、正文或顺序发生变化时，仍调用 `removeAllItem`，重新创建整页控件。**发送确认是否触发重建取决于签名中消息 ID、时间等字段是否发生变化，不能一概而论。

可能影响：消息密集时重复创建 QWidget、QTextEdit、布局和图片，增加主线程工作；每页历史同步也会产生本地回读。这是“反复重建最多最近 50 条”，不是“把全部历史一次性显示”。

处理方向：按消息稳定标识增量插入、更新状态、删除窗口外消息；同一会话的连续刷新合并一次；批量历史落库后避免每页都重建当前视图。保留去重、事务和同步游标语义。

### P1：图片解码、缩放和原图驻留仍在主线程

入口：`dialogs/chatpage.cpp` 的 `AppendChatMsg`；`dialogs/chathistorydialog.cpp`；`widgets/picturebubble.cpp` 的 `setPicture`。

确定的工作：收到压缩字节后执行 `QImage::fromData`，转换 QPixmap，再平滑缩放预览。PictureBubble 同时保存解码后的原图 `_picture`。压缩字节缓存命中不能避免再次解码；整页重建会放大重复处理。

可能影响：多个高分辨率图片出现时，主线程卡顿和内存增长。64 MiB 的压缩字节缓存上限不等于所有气泡原图、缩略图、草稿和下载残片的总内存上限。20 MiB 的文件大小上限也不能替代像素尺寸预算。

处理方向：后台用 QImageReader 生成预览，按文件 ID、目标尺寸和 DPR 缓存；GUI 只接收结果、生成显示对象；点击查看时才加载原图。使用像素和内存预算，同时保留原始文件质量。格式是否支持解码阶段缩小需要检查，不能保证所有格式都跳过完整解码。

### P1：粘贴图片和输入内容整理占用主线程

入口：`widgets/messagetextedit.cpp` 的 `insertFileFromUrl`、`insertImage`、`insertImageData`、`messages`；`dialogs/chatpage.cpp` 的 textChanged 回调、`SaveCurrentDraft`。

确定的工作：文件读取、粘贴图片 PNG 编码、解码、缩略图生成在输入处理路径同步执行。每次 textChanged 都调用 `messages()` 扫描文档；500 ms 草稿保存定时器已经存在，但它没有延迟这一轮文档扫描。

可能影响：大图粘贴停顿、带附件的长草稿输入响应变差。草稿落库还会删除并重新插入草稿内容，附件 BLOB 可能重复写入。

处理方向：文件读取和图片处理进入有并发上限的后台任务；textChanged 只标记草稿变脏，在防抖保存或发送时解析；附件内容与草稿文本状态分离，避免相同图片反复写入。截图捕获及界面操作仍遵守 GUI 线程要求。

### P1：图片任务没有全局并发上限

入口：`network/resourcemgr.cpp` 的 `fetchImage`、`downloadImage`、`uploadImage`。

确定的工作：同一图片已有 `_fetching` 去重，但不同图片各自创建 QThread；没有全局传输并发队列。账号 generation 能拒绝过期结果，却没有主动停止已经开始的网络循环。失败下载的 `_partials` 没有单独的总内存预算。

可能影响：同时显示很多未缓存图片时出现大量线程、连接、内存和进度事件；退出账号后旧任务仍继续传输；多次不同图片失败可积累下载残片。

处理方向：有界任务调度器，先以可配置的少量并发开始验证；主动取消旧账号任务；进度通知节流；为下载残片设置总预算、过期清理和账号边界。不能只把“一图一线程”换成无限排队的线程池。

## 3. 次优先处理

| 项目 | 当前源码行为与可能影响 | 建议 |
| --- | --- | --- |
| 会话排序和搜索 | `ConversationSection::UpdateThread` 每次更新调用 sortItems 和 RefreshSearch；AddThreads 逐项更新，重复排序。主搜索 textChanged 立即扫描、清空并重建结果控件 | 批量更新后排序一次；主搜索防抖；只刷新受影响结果，限制结果数量 |
| SQLite 队列与附件 | `ChatStore::Execute` 串行执行任务；SaveAttachment / LoadAttachment 在同一数据库工作线程进行文件读写和 SHA-256。大附件可拖延消息、查询和草稿任务 | 合并过期任务，监控排队深度；附件 IO/校验与 SQL 分工，数据库连接仍由唯一所属线程使用 |
| 同步日志 | `common/logger.cpp` 每条消息加锁写文件、flush，控制台也 fflush；TCP 请求/响应会记录日志 | 有界后台日志队列、批量刷写、日志轮转；重要失败和退出可靠刷写。QFile::flush 不能直接等同磁盘 fsync |
| 本地会话设置 | `ConversationPreferences` 每次保存 settings.sync；多会话未读状态变化可连续触发同步保存 | 自动状态变化合并保存，账号退出可靠刷写；保留状态未变时跳过保存的现有判断 |
| 大历史查询 | SearchMessages 含 LIKE 前置通配符、日期 substr、COALESCE 排序和 OFFSET | 用真实数据做 EXPLAIN QUERY PLAN；评估日期范围和游标分页；全文索引需要确认中文分词与搜索语义 |
| 表情选择器 | 搜索或分类改变时 RebuildContent 销毁并创建按钮；当前两个目录合计 446 项，不是每次都显示全部 | 复用分类页、图标缓存、搜索防抖；必要时用 model/delegate。不要把 QWidget 创建移到后台线程 |
| TCP 突发消息 | readPackets 在一个回调中循环处理所有完整帧，JSON 和后续信号在主线程执行 | 测量突发时事件循环延迟，必要时按时间预算分批处理；保持接收顺序，不凭空拆到多个并行处理器 |
| 关闭等待 | ChatStore::Shutdown 使用 BlockingQueuedConnection 并 wait，等待此前排队操作 | 测量退出时积压量，提前停止新任务并收敛队列；不能直接丢弃已接受但未完成的消息写入 |

历史记录对话框已有搜索定时器，不能与主搜索框没有防抖混为一谈。SQL 的具体扫描量和排序代价尚未通过执行计划确认。

## 4. ResourceServer 的扩展瓶颈

入口：`resource_server/main.cpp`。

1. **所有文件请求共用一把 `filesMutex_`。**该锁覆盖元数据读写、上传写入、下载读取和 Base64，以及上传结束时整文件 SHA-256 和部分 Redis 操作。不同文件的操作也相互等待，因此增加连接线程无法消除这个串行区。
2. **每个连接新建 detached 线程，无全局连接上限和可见的空闲读超时。**`ServeClient` 使用阻塞 asio::read/write，空闲或慢连接长期占用线程。需有界连接管理、超时、线程生命周期管理；更大规模再评估异步会话。
3. **每个分块请求都重新连接 Redis 做认证。**`Authenticated` 每次调用 ConnectRedis，上传完成时 MarkReady 也重新连接。应使用可复用连接/连接池，并明确认证失效策略，不能为了速度移除鉴权。
4. **32 KiB 分块逐块请求并等待响应。**客户端 exchange 串行完成请求/响应，服务端每块重读元数据和打开文件；Base64 增加编码和传输工作。在高往返延迟环境可能限制吞吐。

建议先缩小锁粒度到文件级别，把不需要全局互斥的工作移出锁；连接及 Redis 资源有界复用。协议扩展必须同时调整两端：现有帧长度是 16 位，不能只把分块大小随意改成几百 KiB。

## 5. 当前已经具备的优化

- SQLite 已在独立工作线程，使用 WAL；消息页和同步游标在一个事务内提交。不能用降低数据可靠性换性能。
- 实时视图从本地读取最近 50 条，历史对话框分页；有消息去重和账号 generation 保护。
- 内容未变化时，聊天页已有签名比较和气泡复用。
- 文本气泡已避免在每次 Paint 时遍历段落并改变高度，改为布局变化时合并更新。
- 联系人头像缩略图、圆角结果已缓存，默认头像共享；联系人使用 delegate 绘制可见行。不同类型行高度不同，不应简单开启 uniformItemSizes。
- 联系人平滑滚动时间已缩短；覆盖滚动条的同步和层级更新已有减少重复工作的处理。
- 主题样式加载位于启动路径；QSS 的行数本身不是已确认的滚动卡顿原因。

这些优化是否足以消除实际卡顿仍需运行测量。

## 6. 推荐实施顺序与验收

第一批：消息增量更新、图片预览缓存与后台处理、输入图片后台处理。它们直接减少主线程大块工作。

第二批：批量排序、搜索/草稿防抖、有界图片调度、SQLite 任务合并、日志和自动设置保存合并。

第三批：ResourceServer 文件锁和连接模型、Redis 复用、协议吞吐优化，以及大历史查询计划优化。

以下是待执行的验收方案，不是本次测试结果：

| 场景 | 观察指标 | 正确性检查 |
| --- | --- | --- |
| 50 条文字与 50 条图片会话冷/热切换 | 主线程事件延迟、切换耗时、解码次数、常驻内存 | 内容、顺序、头像、图片比例和原图质量 |
| 单会话连续收发及多会话突发消息 | 气泡创建/销毁次数、数据库任务排队深度、GUI 延迟 p95/p99 | 不重复、不漏消息；未读、置顶和时间排序正确 |
| 100 / 1000 联系人滚动与搜索 | 重绘耗时、头像缓存命中、排序次数 | 异高分组、选中状态、拖动边缘与滚动条正确 |
| 大分辨率图片粘贴、截图、附件草稿 | 主线程编码耗时、内存峰值、草稿保存次数 | 切换会话、重登录和发送时草稿不串会话 |
| 1 万 / 10 万条本地历史搜索 | 执行计划、查询耗时、深分页耗时 | 日期、类型、中文搜索和翻页不重不漏 |
| 多图片并发、断网、重试、退出账号 | 活跃线程/连接数、残片总字节、事件积压、取消完成时间 | 旧账号回调不更新新账号；校验和断点续传正确 |
| 多客户端同时传输不同文件 | 服务端锁等待、Redis 连接数、传输吞吐 | 文件权限、大小、完整性和 ready 状态正确 |

GUI 重绘、布局、QWidget 操作留在主线程；适合后台处理的是字节读写、图片 QImage 解码、编码、校验等。跨线程结果使用 queued 信号或 invokeMethod 返回，不直接操作界面。

## 7. Qt 参考资料

以下为 Qt 6 官方文档；实际实现需核对本机 Qt 6.5.3 支持的 API，不能使用后续版本新增接口。

- [Threads and QObjects](https://doc.qt.io/qt-6/threads-qobject.html)：线程归属、GUI 主线程限制和 queued 回调。
- [QImageReader](https://doc.qt.io/qt-6/qimagereader.html)：独立实例可重入，读取尺寸与 setScaledSize；缩小读取效果取决于格式支持。
- [Model/View Programming](https://doc.qt.io/qt-6/model-view-programming.html)：模型、视图和 delegate 的职责划分。
