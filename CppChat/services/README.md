# 客户端流程服务

按完整业务流程划分，不按单个按钮或协议请求拆一个类。流程服务
在 GUI 线程编排已有异步组件，不创建额外工作线程或新单例。

| 类 | 持有者 | 职责 |
| --- | --- | --- |
| `MessageSender` | `ChatPage` | 校验与文本分段、UUID、发送前落盘、图文排队、上传后的元数据落盘、TCP 发送、失败和手动重试、确认超时 |
| `ChatSyncService` | `ChatDialog` | 打开账号存储、恢复会话与游标、会话分页、历史队列、整页校验和提交、实时通知/回执合并、本地读取合并、每分钟补拉 |
| `ClientUpdateService` | `main.cpp` 的应用作用域 | HTTPS 更新清单、版本比较、受限下载、大小与 SHA256 校验、向 Inno 安装器交接已验证文件；`UpdateDialog` 管理提示，主窗口析构后才交接 |
| `VoiceCallService` | `VoiceCallDialog` | 通话状态、控制确认、ID/对方校验、序号去重、超时和断线释放；不访问设备或界面 |
| `VoiceAudio` | `VoiceCallDialog` | Qt 默认音频设备、Opus、输入格式转换、静音和有限播放缓冲；不访问账号或网络 |
| `VideoMedia` | `VoiceCallDialog` | Qt 默认摄像头、受限 JPEG 编解码和本地预览；不访问账号、网络或音频 |

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
结构化 `history`，GitHub Release 同样使用累计说明。更新窗口只显示通过 HTTPS 清单确认的
`当前版本 < 记录版本 <= 服务端最新版本`，按版本降序排列；尚未检查、检查失败、已是最新版或本机版本更高时
隐藏记录区，不再用内置历史填充。未提供结构化历史的旧清单保留最新版 `notes` 兼容路径。
版本排序和区间判断复用现有 [Qt 6 QVersionNumber](https://doc.qt.io/qt-6/qversionnumber.html)，
无需额外库或跨 Qt Kit 的版本比较实现。每次发布先更新版本和历史记录，累计说明不得超过旧客户端 8000 字符限制。
仓库主页只保留 [Releases](https://github.com/Algebraaaa/CppChat/releases) 入口，版本变更及历史验证记录放在 Release 说明中。

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

## 一对一语音通话

好友聊天工具栏的麦克风按钮发起呼叫，来电窗口可接听或拒接，主叫可取消；
接通后支持静音、时长和挂断，关闭窗口等同挂断，群聊天不启用该按钮。
仅服务端确认接通后开启麦克风；设备缺失、权限拒绝或移除会结束通话。
当前使用系统默认设备，要求支持 48kHz Int16/Float 单声道或双声道。
建议戴耳机，目前没有声学回声消除、设备选择器或群语音。

`VoiceCallDialog(.ui) → VoiceCallService → TcpMgr` 复用登录会话和正式版 TLS；
`VoiceAudio → Opus → TcpMgr → ChatServer → 对方` 不保存音频，不进入历史/离线消息。
Redis Lua 原子维护双用户占用、登录绑定、40 秒来电租约和 20 秒通话租约。
两端每 4 秒保活，任一端 15 秒未续约即拒绝媒体；客户端 12 秒未获控制确认就结束。
每帧 20ms、至多 512 字节、每方向 60 帧/秒（突发上限 10 帧）、序号严格递增；
客户端发送积压 8KiB、服务器队列 10 条时丢音频，播放队列至多 6 帧。

复用来源与取舍：使用现有 [Qt QAudioSource](https://doc.qt.io/qt-6/qaudiosource.html) /
[QAudioSink](https://doc.qt.io/qt-6/qaudiosink.html) 和未修改的
[Xiph Opus 1.5.2](https://opus-codec.org/release/stable/2024/04/12/libopus-1_5_2.html)。
源码 SHA256 固定在 `cmake/VoiceAudio.cmake`，BSD COPYING 随安装包分发。
该版本有上游编解码测试及 Windows AVX2 修复；已查许可证、发布记录及
[安全公告](https://github.com/xiph/opus/security)；未发布公告不等于不存在漏洞。
Qt 继续动态链接并沿用现有许可方式，CI 显式安装 qtmultimedia。
也评估了 [libdatachannel](https://github.com/paullouisageneau/libdatachannel)（MPL-2.0、ICE/SRTP），
但会新增 ICE/SRTP、TURN 部署和 MinGW 依赖；当前中转可满足本次需求，故不引入整套 WebRTC。

安全边界：这是 **TLS 传输加密的服务器中转，不是端到端加密**，可信服务器可接触音频。
现有节点间 gRPC 是明文私网方案，语音仅允许 loopback，两聊天节点需在同一主机；
客户端在不同网络仍复用原公网 TLS 端口，不新增端口或要求 TURN。
跨主机节点需先给 RPC 加 TLS/mTLS，当前直接拒绝该语音路由。
TCP 丢包有队头阻塞风险；缓冲虽有限，不承诺弱网延迟或公网容量指标。
旧服务端不声明 `voice_call=true` 时客户端提示不支持，上线需更新两个节点和客户端。

## 一对一视频通话（2026-10-18）

好友工具栏的视频按钮使用同一 VoiceCallDialog/VoiceCallService。邀请和接听都必须声明
`video=true`，服务端原子状态记录该类型；语音通话不能私自升级，旧语音客户端不能以缺省
语音接听视频邀请。只有双方确认接通才启用麦克风和摄像头；Esc、关闭窗口、挂断、好友
删除、会话替换和超时复用现有释放路径。关闭摄像头停止本地采集、清除双方预览并告知
对方，音频继续；再次开启恢复采集，设备失败会明确结束通话。

VideoMedia 使用 [Qt 6.12.0 QCamera/QMediaCaptureSession/QVideoSink](https://doc.qt.io/qt-6/qmediacapturesession.html)
采集，使用 [Qt Gui QImageWriter](https://doc.qt.io/qt-6/qimagewriter.html) 和 QImageReader 编解码 JPEG。
参考 [Qt 同版本 Camera 示例](https://github.com/qt/qtmultimedia/blob/v6.12.0/examples/multimedia/camera/camera.cpp)
的公开接口方式，没有复制示例代码；继续使用既有 Qt 动态库及 LGPL/第三方许可、SBOM 和
qjpeg 插件，不新增第三方库。Qt 同时维护 Windows/Linux 后端及其上游测试，但本轮设备
验收仍限实际记录，不据此声称所有后端通过。

视频至多 320×240、8fps，JPEG 每帧 6000 字节（Base64 8000 字节）。复杂画面先缩小到
240×180 或 160×120，仍过大则丢弃；接收先检查 JPEG 头部尺寸再分配图像，不接受任意
格式、超大图片或历史/非本次通话帧。画面和音频具有独立序号和速率桶；客户端积压
8KiB、服务器实时队列 10 项时丢帧，不录制、不落库、不补发媒体。关闭摄像头时服务器
也拒绝该方向的视频帧，客户端丢弃迟到画面。界面和设备均由 GUI 线程协调，旧设备
回调通过 generation 检查失效。

服务端中转沿用语音的可信服务器、同机 RPC、TLS 和 TCP 队头阻塞边界。JPEG 更易复用
当前 Qt/MinGW，代价是较低清晰度、较高带宽且没有音视频时钟同步、硬件视频压缩或
回声消除；不承诺弱网质量。需要更高清、多人或大规模媒体路由时应另行评估 WebRTC/SFU。
没有摄像头/权限、真实设备热拔插、真人回声与跨公网延迟必须人工验证。正式部署还需
更新客户端及两个聊天节点，服务端登录声明 `video_call=true` 后才允许发起视频。
