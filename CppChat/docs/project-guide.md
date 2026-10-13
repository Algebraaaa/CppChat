# 项目说明

2026-10-04 补充：根目录 [README](../README.md) 提供开发入口和本机验证环境。
本轮优化与清理记录见 [性能优化进度](performance-improvements.md)。
2026-10-06 按用户要求删除客户端 `scripts/` 和 `tests/`，工程不再登记这些文件。
性能文档中的测试结果与旧路径作为历史验证记录保留，不是当前可运行入口。
2026-10-12：图片后端已移至仓库根目录 `ResourceServer/`，客户端目录只保留图片传输客户端；下文清理记录中的 `resource_server` 路径为历史位置。

这里汇总源码目录与历史迁移记录。界面使用见 [界面功能](ui-features.md)，本地消息存储见 [本地聊天记录](local-chat-history.md)。

## 源码目录与维护约定

### 文件职责

| 位置 | 内容 |
| --- | --- |
| `app/` | 启动入口、主窗口、主题加载 |
| `common/` | 数据模型、协议常量、校验、日志和公共工具 |
| `network/` | HTTP、TCP、资源传输和用户会话管理 |
| `services/` | 完整消息发送与历史同步流程，不持有控件；边界见 [客户端流程服务](../services/README.md) |
| `dialogs/` | 业务页面、弹窗及其关联列表项，`.h/.cpp/.ui` 放在一起 |
| `dialogs/conversationsection.*`、`dialogs/contactssection.*` | 完整聊天和通讯录模块，结构见 [四模块导航](ui-features.md#navigation) |
| `dialogs/conversationmenu.*`、`common/conversationpreferences.*` | 会话右键菜单和本机置顶/未读设置，说明见 [会话菜单](ui-features.md#conversation-menu) |
| `widgets/` | 可复用控件、消息编辑器、气泡、滚动和图片裁剪 |
| `widgets/screencapture.*` | 多显示器截屏、区域框选与窗口恢复，使用说明见 [聊天截屏](ui-features.md#screen-capture) |
| `widgets/contactuserlist.*` | 联系人分类、折叠、排序和行绘制，说明见 [联系人界面](ui-features.md#contacts) |
| `style/` | 主题清单、公共参数、控件和页面 QSS |
| `Resources/` | 程序内置图片、SVG、字体及其来源许可 |
| `static/` | 现有运行时兼容资源，构建后复制到程序目录 |
| `../../ResourceServer/` | 根目录的独立图片后端，使用服务端公共 CMake 配置；客户端只通过网络访问 |
| `third_party/` | 第三方依赖原始源码、版本说明和许可证，目前包含 spdlog |
| `docs/` | 项目说明、界面功能、本地聊天记录和面试资料 |

`CMakeLists.txt` 管理构建选项、入口和 Qt 运行时部署，并收集
`app/common/network/services/dialogs/widgets` 内的源码、头文件和 UI；资源登记在 `rc.qrc`。
`CMakePresets.json` 提供独立的 Debug / Release 构建目录；
Qt Creator 可按实际目录查看 `app/common/network/services/dialogs/widgets/docs/third_party`。

服务端沿用 Visual Studio 的虚拟分类：启动入口和服务接口放在 `Application`，
业务逻辑与联系人标签校验放在 `Core`，网络、MySQL、Redis、gRPC 和公共工具
放在对应的 `Infrastructure` 分类。Proto 及生成代码放在 `Generated`，
配置放在 `Configuration`，数据库初始化脚本放在 `Database/Schema`，
迁移脚本放在 `Database/Migrations`。这些分组由 `.vcxproj.filters` 管理。

### 源码约定

- 客户端文件名统一使用小写；类名保留 Qt 风格，例如 `TextBubble`。
- 项目头文件的包含路径必须与真实文件名大小写一致；UI 提升控件的头文件也遵守这个约定。
- 保留现有公开函数和信号名称，新增代码按所属模块的职责放置。
- C++ 文件使用 UTF-8。`.clang-format` 统一两空格缩进、括号、换行和指针书写。
- 格式化不调整头文件顺序，保留 Windows 平台头文件的先后依赖。
- `.ui` 负责布局，QSS 负责外观，C++ 负责行为；主题维护位置见 `style/README.md`。
- 注释说明职责、原因和边界；第三方作者及许可信息保留。

### 新增文件

1. 将客户端代码放到对应模块；有关联 UI 时，将三个文件放在一起。
2. 客户端已有模块内的新 `.cpp/.h/.ui` 会由 CMake 自动收集；新模块须更新 `CMakeLists.txt` 的源文件目录。
3. 新增内置资源时登记到 `rc.qrc`；新增 QSS 模块时还要登记 `style/theme.json`。
4. 在 Qt Creator 重新运行 CMake 后构建。

日常资源检查：在 `rc.qrc` 核对资源路径，在 `style/theme.json` 核对主题模块，
重新运行 CMake 并构建客户端，再检查对应页面。

构建产物放在 `build/`；本机 Qt Creator 配置、日志和迁移备份由 `.gitignore`
排除。此前专项验证用的四个测试源码与工程文件已删除。2026-10-02 清理了空的 `tests/` 目录；三个旧测试构建目录从 `build/` 移入 `.migration-backup/obsolete-test-builds-20261002/`，需要时可以恢复。后续优化增加的客户端测试及维护脚本于 2026-10-06 按用户要求删除。

2026-10-02 清理了重构后不再引用的 `AddUserItem`、`ConUserItem`、`StateWidget`
共 8 个源码、头文件和 UI 文件，以及 22 个已被现有图标替代或仅供旧控件使用的资源。
工程清单、资源清单和旧控件样式同步移除，原文件与修改前清单保存在
`.migration-backup/unused-files-20261002-143254/`，需要时可恢复。
头像资源仍可能由服务端返回的路径动态引用，因此保留；构建产物、日志、
资源服务数据、第三方许可与上游原始素材不属于本次清理范围。

上述 `.migration-backup` 路径为当时的备份记录，目前已不在项目目录。
2026-10-06 递归清理将 `build/performance-validation/`、
`resource_server/build-logging/`、残留的 `scripts/` Python 缓存、
Qt Creator 索引缓存和旧日志移入 Windows 回收站。
保留正常构建目录、配置、聊天数据库和资源服务数据，以及各组件最近一次运行日志。

<a id="migration"></a>

> 以下迁移记录写于 2026-09-13，描述当时的实现。后续增加的图片消息、本地聊天记录和界面功能，以现有源码及对应章节为准。

## llfcchat 客户端功能迁移说明

参考目录：`E:\Project\llfcchat\client\llfcchat`。
目标目录：`E:\Project\Qtemp\myPractice\CppChat`。
迁移日期：2026-09-13。

### 已接入的功能

| 功能 | 实现位置 | 迁移后的行为 |
| --- | --- | --- |
| 登录数据初始化 | `network/tcpmgr.cpp`、`network/usermgr.cpp` | 登录回包保存个人信息、好友与申请列表，登录后才初始化聊天业务 |
| 好友搜索 | `widgets/searchlist.cpp`、`dialogs/findsuccessdlg.cpp` | 未找到可关闭；找到已有好友可跳转会话；自己不能添加自己；失败与超时结束加载 |
| 申请与接受好友 | `dialogs/applyfriend.cpp`、`dialogs/applyfriendpage.cpp` | 发送申请、接收实时通知、接受申请、回包成功后更新按钮与联系人；接受流程复用现有申请表单，避免复制 AuthenFriend 的标签代码 |
| 联系人列表和详情 | `widgets/contactuserlist.cpp`、`dialogs/friendinfopage.*` | 使用真实好友数据，每次显示 13 条；详情查看姓名、昵称、备注并发起私聊 |
| 会话列表与创建私聊 | `dialogs/chatdialog.cpp` | 加载服务端会话、创建/打开私聊、列表去重、最后消息预览和未读红点 |
| 历史消息 | `dialogs/chatdialog.cpp`、`common/userdata.*` | 选择会话时按需加载历史；使用独立历史游标，避免实时消息推进最后 ID 后漏掉旧消息 |
| 文本收发 | `dialogs/chatpage.cpp`、`network/tcpmgr.cpp` | 点击发送和 Enter 共用流程；按真实 UTF-8/JSON 大小分包；通过 unique_id 将发送中的消息与服务端回包关联 |
| 消息状态 | `widgets/chatitembase.cpp` | 显示发送中、已发送、发送失败；不将服务器回执声称为对方已读；晚到确认更新失败状态 |
| 会话切换 | `dialogs/chatpage.cpp` | 清理旧气泡，展示对应会话消息；保留每个会话的文字草稿 |
| 个人资料和头像 | `dialogs/userinfopage.*`、`network/usermgr.*`、ChatServer | 点击侧栏头像进入资料页；编辑用户名、昵称、描述，裁剪头像后点击“保存资料”同步到服务器 |
| 登录生命周期 | `app/mainwindow.cpp` | 主动退出、连接断开或异地登录后回到登录页，清理会话和弹窗并恢复登录按钮 |
| 心跳 | `network/tcpmgr.cpp` | TCP 登录成功后每 10 秒发送 1023，收到 1024 更新计时；客户端 30 秒无有效回包关闭连接；服务端 20 秒无数据视为超时并每 10 秒扫描；退出和断线停止计时 |

`SmoothScrollList` 的滚动实现保持原样，三个业务列表继续复用它。原有标题栏、窗口动画与认证页样式保留。新页面样式已加入 `style/stylesheet.qss`，CMake 已收录新增源码和界面文件。

### 参考项目中的未完成功能

参考项目没有完整实现群聊、语音视频和图片/文件聊天消息的网络传输。个人资料更新是 2026-09-29 后续补充的协议，不依赖参考项目的占位实现。

图片和文件仍可在输入框本地预览；发送时若包含附件，会明确提示当前只能发送文本，并保留输入内容。好友表单的标签编辑仍为本地交互，现有服务端申请协议没有标签字段。

资料页使用 1031/1032 保存用户名、昵称、描述和头像；1033/1034 按 uid 获取头像。服务端按已登录 TCP 会话识别修改者，头像保存在 `user_avatars` 表中；其他客户端重新登录或搜索时可取得最新文字资料，打开好友详情时会重新请求头像。现有登录列表没有个人资料变化的实时推送。

### 协议与联调边界

- TCP 仍为 2 字节消息 ID + 2 字节 UTF-8 消息体字节数 + JSON，包头为大端。
- 本次与服务端请求/回包字段进行了静态对照；两个聊天工程现共用 `../../ChatServerShared/LogicSystem.cpp`。客户端兼容 `message_id/msg_id`、`content/msg_content` 两组字段名。
- ChatServer1 和 ChatServer2 的 `MAX_LENGTH` 已由 2KB 调整为 16KB。原来的 2KB 上限会使 `CSession::Send()` 拒绝合法的登录或历史回包；实测 10 条历史消息序列化为 2540 字节，服务器日志明确记录了拒绝发送。16KB 小于服务端有符号 `short` 长度字段的上限，并与两个聊天服务器保持一致。
- 跨服务器实时聊天的 gRPC `TextChatMsgReq` 已补齐 `thread_id`、数据库 `message_id`、时间和状态。目标服务器把 1019 通知统一组装为 `chat_datas`，与同服通知和客户端解析格式一致；ChatServer1 的对端名称也统一为 `ChatServer2`，保证双向查找 gRPC Stub 时大小写一致。
- 历史加载失败可重新点击对应会话重试；会话列表加载失败可点击侧栏聊天图标重试。错误恢复不会无限保持加载弹窗。
- 文本分段额外考虑服务端回包中正文重复和 Unicode 转义的开销。普通客户端消息仍主动控制在 2KB 内；资料更新包可达 16KB，头像先压缩到不超过 8KB。服务器接收和回包方向允许最多 16KB。

### 微信式聊天布局调整

实现前检查了两个 MIT 许可的 Qt/C++ 开源聊天客户端：

- `mowangshuying/WeChat`：采用固定窄导航栏、会话栏和聊天内容区；输入区单独设置表情、文件、截图和语音工具栏。
- `flyjay123/OurChat`：Qt 6 客户端使用会话列表、聊天记录区和底部编辑器的三段式结构。

当前项目已经有 `QSplitter + ChatUserList + ChatView + MessageTextEdit`，因此保留现有控件、TCP 业务和消息模型，只吸收布局组织方式，没有复制参考项目源码或图片资源。聊天页现在使用浅色功能栏、整行绿色会话选中态、浅灰聊天背景、居中时间分隔和微信式输入工具栏。表情、图片和发送沿用现有功能；小程序、截图、语音、会话设置、看一看、手机和更多入口仅显示占位提示。

### 已执行检查与尚未执行的验证

已完成源码静态核对：工程文件存在性、重复项目项、19 个 UI 文件结构和控件引用、提升控件头文件路径、QSS 括号、13 个 TCP 回包/通知处理器。

使用本机 Qt 6.5.3 的 `uic` 和 `moc` 解析了原迁移输入文件，并再次单独检查本次修改的 `chatdialog.ui`、`chatpage.ui` 和 `chatuserwid.h`。该检查验证 UI 与元对象声明可以生成代码，**不等同于 C++ 编译、链接或运行测试**。

按之前由你编译验证的方式，本次没有编译或启动客户端/服务端，没有宣称完成端到端联调。

### 编译与人工验证顺序

1. Qt Creator 打开客户端 `CMakeLists.txt`，运行 CMake 并重新构建，以收录新增 `.cpp/.h/.ui` 文件。
2. 登录一个没有好友的新账号：应显示真实空列表，不再出现测试用户；搜索自己、不存在用户均可正常退出弹窗。
3. 用 A/B 两个账号登录：A 申请添加 B；B 收到红点，在“新的朋友”接受；双方联系人应更新，不出现重复行。
4. 分别让两个账号连接 ChatServer1 和 ChatServer2，双向发送中文、emoji、多行和长文本；接收端应立即出现气泡，检查预览、回执及切换会话后的顺序。
5. 退出后重新登录，检查历史消息；换账号登录，确认没有上一账号的聊天记录和弹窗。
6. 重新编译并重启 ChatServer1/2 后，打开已有消息的会话，检查 1030 历史回包及历史气泡；再保持连接检查 1023/1024 心跳。断开聊天服务器后应回到登录页并可重登；同账号另开客户端登录应使旧连接下线。
7. 点击侧栏头像，修改用户名、昵称、描述并选择裁剪头像，点击“保存资料”；重新登录验证资料来自服务端，用另一客户端登录或搜索验证新资料和头像；重复用户名应收到冲突提示，取消裁剪不改变头像。
8. 验证三个业务列表的悬浮滚动、联系人翻页，以及登录/注册/聊天之间的窗口大小仍符合现有效果。

### 来源与备份

会话/消息模型、好友业务、资料页布局和裁剪控件来自上述本地参考项目，并按当前目录、Qt 6 与协议修正。`imagecropperlabel.h` 保留了原作者 Leopard-C 的署名。

迁移前源码和 QSS 已备份到：`.migration-backup/before-llfcchat-20260912-233430.zip`。备份不含构建产物，也没有覆盖参考目录中的文件。

本次修改原有文件 35 个，新增源码/界面文件 10 个（不含本说明与临时检查文件）。
