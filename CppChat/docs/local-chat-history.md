# 本地聊天记录实现与验收

本轮源码已接入 Qt 的 QSQLITE 驱动。MySQL 保留服务端聊天记录，本机 SQLite 保存消息、同步位置、待确认消息和草稿，图片缓存放在账号目录下的文件中。本轮没有编译 C++、启动服务或修改运行中的 MySQL。

## 复用的项目与资料

- [Qt qtbase](https://github.com/qt/qtbase)：直接使用已有 Qt 环境的 Qt SQL 模块和 QSQLITE 驱动，无需额外 SQLite C++ 包装库。项目和公共 client.pri 已添加 `QT += sql`。
- [TDLib](https://github.com/tdlib/td/blob/master/td/telegram/MessageDb.cpp)：参考其 SQLite 消息表、索引、事务与异步数据库职责划分；未复制源码，也未引入 Telegram 的协议栈。
- [SQLiteCpp](https://github.com/SRombauts/SQLiteCpp)：评估过的成熟包装库；本项目已有 Qt，继续使用同一套数据库 API。
- [Qt QSQLITE 文档](https://doc.qt.io/qt-6/sql-driver.html#qsqlite)、[QSqlDatabase 线程约束](https://doc.qt.io/qt-6/qsqldatabase.html)、[SQLite WAL](https://www.sqlite.org/wal.html)。

## 存放位置与隔离

2026-10-06 调整：开发运行时从 EXE 目录向上寻找 `CppChat.pro`，默认保存在项目根目录的 `chat_data/`，不受当前工作目录影响。数据库使用 `sql/db.sqlite` 布局：

```text
<项目根目录>/chat_data/accounts/<GateServer URL 的 SHA256 前 24 位>/<uid>/
  sql/db.sqlite
  sql/db.sqlite-wal / sql/db.sqlite-shm  # SQLite 打开期间可能存在
  attachments/<文件 ID 的 SHA256>
```

登录成功后日志输出实际数据库路径。同一 GateServer 后面的 ChatServer1、ChatServer2 共用账号缓存；不同 GateServer、不同 uid 使用不同目录。账号切换会关闭旧连接并增加登录代数，数据库、图片传输和发送计时器拒绝旧代数回调。

本项目当前默认数据根目录为 `../chat_data/`，不再随 Debug、Release 或 build 子目录变化。不创建项目外独立数据目录。发布目录没有 `CppChat.pro` 时，使用 EXE 同级的 `chat_data/`。目标在 C 盘时拒绝打开数据库；目标不可写时显示错误，不自动改存 AppData 或其他系统盘目录。暂不提供目录选择界面。

首次登录某个账号时，如果新位置还没有 `sql/db.sqlite`，依次检查项目账号目录中的旧 `chat.db`、当前 EXE 同级的旧账号目录、旧 `QStandardPaths::AppLocalDataLocation/accounts/...`。采用找到的第一份数据库，不自动合并多份旧库。工作线程用 [SQLite VACUUM INTO](https://www.sqlite.org/lang_vacuum.html#vacuum_with_an_into_clause) 生成完整快照（包含已提交的 WAL 数据），并复制该账号图片缓存；临时数据也位于目标盘。全部完成后才发布正式目录；同目录布局升级只发布 `sql/` 子目录。失败时保留原数据并允许重试。新位置已有数据库时不重复迁移、不覆盖，旧库作为恢复副本保留。

本版使用普通 SQLite，数据库、草稿及图片均未加密。没有保存登录密码或 token。SQLCipher 和系统密钥存储尚未接入。

### 2026-10-06 目录调整验证

Qt 6.5.3 / MinGW GCC 11.2.0 的隔离客户端 Debug 构建通过。
临时验证程序直接使用当前路径及迁移代码，验证项目根目录定位、`sql/db.sqlite` 布局、
已提交但仍在 WAL 中的记录、附件字节一致、已有目标库不覆盖和同目录布局升级。
验证数据当时位于项目的 `.validation/storage-layout/`（随后按用户要求清理），不使用真实账号数据库，
不重新添加客户端 `scripts/` 或 `tests/` 目录；真实账号在下一次登录时迁移。

随后按用户要求现场迁移本机现有账号：新 `sql/db.sqlite` 完整保留 22 条消息、12 个会话、
12 条同步状态，无草稿或缓存附件。SQLite 完整性检查及所有业务表逐行核对通过；
Debug 旧运行目录和旧 AppData 中的数据一致，旧数据库及 WAL/SHM 文件已删除。
常用 Debug 运行目录的 EXE 已更新为本轮通过编译的新程序，后续启动使用项目根目录数据。

## 数据流

2026-10-06 架构整理：`services/messagesender.*` 负责发送完整流程，
`services/chatsyncservice.*` 负责恢复、分页同步和实时消息/回执落盘合并；
`ChatPage`、`ChatDialog` 通过信号刷新 UI，SQLite 事务和工作线程仍由 ChatStore 管理。
最终 Debug/Release 编译及隔离回归结果见 [架构加固第三步](architecture-hardening-20261006.md#第三步客户端完整流程分离)。

1. **登录**：在专用工作线程创建/打开数据库，检查 `user_version`，开启 WAL 和 `synchronous=FULL`。读取会话与上次提交的同步位置。仍是好友的会话恢复本机最近 50 条记录和待确认消息；好友姓名、头像仍使用登录时获取的资料。
2. **历史同步**：复用 `ID_LOAD_CHAT_MSG_REQ`，从该会话的 `confirmed_cursor` 开始向后补拉。整页格式、会话、顺序和游标通过校验后，同一个 SQLite 事务保存消息和新游标，提交成功才请求下一页。登录会补拉全部有效会话；在线每分钟再核对一次。
3. **实时通知与回执**：先保存再更新界面。按 `(thread_id, server_id)` 和 `(thread_id, sender_uid, unique_id)` 合并。实时最大 ID 不会直接推进历史同步游标；重复通知不会再次标成未读。
4. **发送**：先把稳定 UUID、分段正文和待发送图片保存到 SQLite，再清空输入框并开始网络请求。待发送记录和草稿删除位于同一个事务。失败时保留输入；事务成功后即使程序关闭，也能恢复待确认记录。
5. **图片**：草稿和未确认图片暂存 BLOB，保证上传之前也能恢复。上传/下载成功的完整图片用 QSaveFile 写入账号目录，并保存路径、大小、SHA256。显示优先读取内存，再校验磁盘缓存，缺失/损坏时重新下载。已落盘图片的确认回执会释放相应临时 BLOB。未完成的下载断点仅保留在内存中，重启后重新下载。
6. **草稿**：输入后延迟 500ms 保存，切换会话、退出时也保存；退出先完成已排队写入，再关闭数据库连接和工作线程。文本中的表情标记与图片字节随草稿恢复；发送中的输入框暂时锁定。
7. **查找记录**：现有聊天记录窗口已接入本地查询。每页 50 条，支持中文关键词、图片、链接、日期、上一页、更早记录和刷新。百分号/下划线按字面搜索，SQL 使用绑定参数。图片复用已有预览控件。普通聊天页保留最近 50 条，更多记录由记录窗口分页查看。
8. **清空本机**：聊天设置里确认后删除本机已确认消息，记录隐藏边界；保留同步位置、图片缓存和待确认消息。后续补拉不会显示清空前的记录。服务端记录不删除。

第一次使用新本机数据库时，现有协议需要从旧消息开始完整补拉；之后按保存的游标增量同步。离线独立登录/浏览入口、服务器最新页优先、语音/文件等新消息类型不在本版中。当前关闭连接仍会回到登录页；本机记录在下次成功登录后恢复。

## 服务端调整与发布顺序

两个聊天工程现已共用一份服务端源码，相关位置：

```text
../../ChatServerShared/MysqlDao.cpp
../../ChatServerShared/MessageService.cpp
../../ChatServerShared/LogicSystem.cpp
```

`AddChatMsg` 继续在事务内锁住对应 private_chat 行。在该锁下查找 `(thread_id, sender_id, unique_id)`：已有相同接收者、正文、类型时返回原 message_id/时间/状态；相同 UUID 不同正文拒绝提交。两台服务器连接同一个 MySQL 时，这个行锁也会串行化跨节点重试。原来的空 UUID 问候记录不受影响，新发送请求要求非空 UUID。

成功登录回包增加 `message_dedup=true`。客户端只在获得这个能力标记时，允许对“未确认”消息右键重试；重试沿用原 UUID；有本机图片字节时重新协商上传断点，避免旧资源就绪信息失效后无法继续发送。旧服务器没有此标记时，重试菜单禁用。启动时不会自动重发。

历史响应根据实际 styled JSON 大小分批，预留包头以外的正文空间，保证不超过现有 MAX_LENGTH。空页明确返回空数组和原游标。个别旧消息如果单条本身就超过包限制，会返回加载失败并保持游标，不能无声跳过。

**部署时同时升级、重启两套 ChatServer，随后重新登录客户端。** 本轮没有执行 MySQL DDL。两套 `schema_migrations/2026-10-02-chat-message-retry-index.sql` 已提供可重复执行的查询索引脚本，建议在备份并核对库名后手动执行，以免每次 UUID 查找扫描消息表；脚本不删除或改写消息。去重依赖所有聊天写入经过上述事务和行锁；数据库本身暂未增加 UUID 唯一约束。可在审计历史 UUID/列定义后补充唯一约束，避免直接对旧问候记录或既有重复数据执行不兼容的迁移。

客户端需重新运行 qmake，然后由你编译。Windows 构建完成后，`CppChat.pro` 会从当前 Qt Kit 自动复制 Qt SQL 库到 EXE 目录，并将 SQLite 插件复制到 `sqldrivers` 子目录；Debug 构建优先使用带 `d` 后缀的 DLL，没有时使用该 Kit 提供的普通 DLL 名称。发布包应包含这些文件，不要从其他 Qt 版本复制插件。

### 启动后没有聊天记录的排查

如果日志出现 `QSQLITE driver not loaded`，说明本地数据库无法打开，因此历史同步尚未开始。先检查 EXE 目录下的 `Qt6Sql.dll` 和 `sqldrivers/qsqlite.dll`（部分 Debug Kit 使用 `Qt6Sqld.dll`、`qsqlited.dll`）。依赖补齐后退出客户端再启动。后续源码版本会在初始化失败时弹出具体错误，并在日志中记录存储失败原因。

2026-10-02 已为当前 `Desktop_Qt_6_5_3_MinGW_64_bit-Debug/debug` 运行目录补齐这两个 DLL，并校验它们与当前 Qt Kit 一致；自动复制规则已通过 qmake 的 Debug、Release 配置生成检查。本次没有编译或重启客户端，聊天记录恢复仍需重新启动后验收。

## 已完成的验证

以下是历史验证记录。2026-10-06 已按用户要求删除客户端 `scripts/` 和 `tests/`，
下列脚本不再包含在当前源码中；联机验收步骤仍见后续章节。

```powershell
python scripts/test_chatstore_sql.py
python scripts/check_style.py
```

数据库测试使用实际打包 schema，并从 `chatstore.cpp` 提取 SQL 执行，数据库全部位于测试临时目录。覆盖 UUID 回执合并、空 UUID 初始消息、事务失败同时回滚消息/游标、实时最大 ID 与同步位置分离、分页不重叠、中文/字面通配符/分类/日期搜索、清空边界及隐藏范围内的晚到回执核销、草稿/附件元数据重开及账号文件隔离；新增迁移快照检查，验证未合并的 WAL、待确认图片、草稿和游标完整保留，以及已有目标数据库不会被覆盖。

这些结果验证 SQL 行为和静态资源，不代表 C++ 编译、Qt 工作线程、窗口交互、图片磁盘校验或 MySQL 并发已经运行验收。

## 编译后需要执行的联机验收

1. 登录、收发文字和表情，退出程序再登录，记录恢复；继续发送，确认数据库没有重复行。
2. 输入图文草稿，切换会话、退出再登录，图文顺序与表情恢复。
3. 在消息发出但回执未到时关闭程序；再登录先补拉核对，已经发送成功的消息合并到原行。对仍未确认的文字和图片右键重试，确认 MySQL 只有一个 UUID 对应的消息 ID。
4. A 经节点 1 发送后，在节点 2 用同一个账号重试同一 UUID；检查原 ID 被返回。两套服务端都必须已升级。
5. 连续收发较长中文消息和图片，检查历史页没有超过 2KB、游标连续、旧消息没有漏掉。
6. 搜索中文、`100%_`、图片、链接、日期并翻页；查找记录的条数与当前隐藏边界一致。
7. 清空本机已确认记录，再登录/补拉，旧记录不重新出现，新记录正常显示。
8. 切换两个账号和两个 GateServer，记录、草稿、图片互不串入；上传过程中切换账号，旧传输不修改新账号界面。
9. 缓存图片后断开资源服务，在聊天记录窗口打开旧图；修改缓存文件后重新打开，校验失败应重新请求资源服务。
10. 对数据库目录拒绝写入或模拟磁盘不足，确认发送不会先清空输入，也不会推进同步位置。
