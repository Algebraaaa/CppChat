# 客户端流程服务

按完整业务流程划分，不按单个按钮或协议请求拆一个类。流程服务
在 GUI 线程编排已有异步组件，不创建额外工作线程或新单例。

| 类 | 持有者 | 职责 |
| --- | --- | --- |
| `MessageSender` | `ChatPage` | 校验与文本分段、UUID、发送前落盘、图文排队、上传后的元数据落盘、TCP 发送、失败和手动重试、确认超时 |
| `ChatSyncService` | `ChatDialog` | 打开账号存储、恢复会话与游标、会话分页、历史队列、整页校验和提交、实时通知/回执合并、本地读取合并、每分钟补拉 |
| `ClientUpdateService` | `main.cpp` 的应用作用域 | HTTPS 更新清单、版本比较、受限下载、大小与 SHA256 校验、向 Inno 安装器交接已验证文件；`UpdateDialog` 管理提示，主窗口析构后才交接 |

固定页面结构继续由 `.ui` 管理。`ChatPage` 保留编辑器、草稿与气泡渲染、菜单和窗口交互；
`ChatDialog` 保留四模块导航、未读/置顶展示、好友和私聊入口。
服务只接收数据及操作，发出带会话 ID 的结果信号，不访问 QWidget、QMessageBox 或页面指针。

## 两条主流程

发送：`ChatPage → MessageSender → ChatStore 保存 → ResourceMgr 上传（图片）→ ChatStore 保存文件 ID → TcpMgr`。
保存成功后才通知页面清空对应会话草稿；文字跳过上传步骤，文字与图片按原顺序进入发送队列。

确认/同步：`TcpMgr → ChatSyncService → ChatStore 事务提交 → UserMgr 合并 → 页面刷新`。
实时通知和发送回执不推进连续历史游标；只有完整历史页提交后才推进并请求下一页。
回执沿用原 UUID 核销待确认记录；重复通知不再次发出新增未读信号。

`ChatStore` 负责 SQLite 事务及自己的数据库线程/附件任务，`ResourceMgr` 负责图片字节传输，
`TcpMgr` 负责连接、组包及协议解析，`UserMgr` 保存当前登录的数据。
流程服务不复制这些基础设施，也不提供按接口拆分的转发包装类。

## 生命周期与边界

- `ChatDialog::ResetSession` 先通过聊天模块 Reset 清空发送状态，再重置同步/关闭账号存储，最后重置资源传输及页面。
- 服务的 QObject 父对象负责析构。异步存储回调使用 QPointer 和账号版本检查；会话删除还核对会话对象及会话版本。
- 同一 UUID 的重试使用独立发送批次，旧批次的 15 秒定时器不能把新批次误判为失败；批次记录到期回收。
- 页面切换不重置服务或数据库；后台结果只刷新所属会话，不强制切回聊天页面。
- 退出/删除不回滚已经提交的本机事务或已经发出的网络请求；这里只阻止旧回调污染当前运行状态。
- `TcpMgr` 为受跟踪的请求每次生成新的 `request_id`，先匹配标识再停止超时/分发结果；业务服务不自己生成传输标识。文本仍按消息 UUID 确认。
- 新服务端声明关联能力后，缺少或不匹配标识的回包被忽略；旧服务端保留原目标字段兼容路径，不能可靠区分所有同目标重试。它不是服务端取消或业务幂等机制，详见 [协议契约](../docs/protocol-contract.md)。

本阶段构建与隔离回归记录见 [架构加固进度](../docs/architecture-hardening-20261006.md)。

## 联系人、更新历史与本机自动登录

联系人备注复用原有 `friend.back`、标签请求/响应及 UserMgr 资料对象。备注与标签在同一事务中保存，
所有者来自认证会话；省略 `back` 的旧客户端不会覆盖备注。编辑保存成功后刷新通讯录、搜索、会话列表和聊天标题；
清空备注恢复账号名。中文分组复用 `common/contactnames` 的离线字典，来源、许可及多音字规则见
`third_party/pinyin-data/README.md`。

`Resources/release-history.json` 是更新记录来源。打包器输出兼容旧客户端的累计 `notes` 及新客户端使用的
结构化 `history`，GitHub Release 同样使用累计说明。更新窗口始终显示历史，包含已是最新版及离线情况；
新旧客户端合并记录按语义版本降序排列。每次发布先更新版本和历史记录，累计说明不得超过旧客户端 8000 字符限制。

自动登录职责分为 `common/autologinstore` 的安全存储、LoginDialog 的单次认证状态及 MainWindow 的启动/退出编排。
当前仅 Windows 启用：复用现有 [DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata)
与私有 ACL，按服务器来源隔离，在聊天服务认证成功后保存加密邮箱和密码；不会保存明文密码或复用一次性聊天令牌。
下次启动只自动尝试一次，可取消或取消勾选；退出登录、异地强制下线、密码失效会清除凭据，临时网络/数据库错误保留。
普通关闭客户端保留加密凭据。其他平台明确禁用该选项，避免使用现有保护模块的非 Windows 明文兼容路径。
同一 Windows 用户下的恶意程序仍可调用 DPAPI；这不是针对已控制当前用户会话的攻击者的隔离。

构建默认 `CPPCHAT_APP_VERSION=AUTO` 跟随源码版本，发布时仍可显式覆盖；旧的手动数字覆盖项需改为 AUTO。
采用 [CMake 缓存与普通变量规则](https://cmake.org/cmake/help/v3.31/command/set.html)，保留版本标签打包能力。


群聊复用 ChatSyncService 的分页、历史补拉、去重和账户隔离；group_profiles 保存群名与成员显示名，
UserMgr 的 group 会话 otherUid=0，不写入好友 UID 映射。MessageSender 复用文本可靠发送，
群图片明确拒绝并保留草稿。GroupDialog 负责单窗口创建和管理，不包含 SQL 或路由逻辑。
群入口在会话列表“+ → 发起群聊”，成员管理在群聊天右上角更多面板中的成员按钮。
