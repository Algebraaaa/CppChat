# CppChat 界面图标

## 消息体右键菜单

`message-languages.svg`、`message-corner-up-right.svg`、`message-box.svg`、
`message-quote.svg` 来自本目录使用的固定 Lucide 提交的 `languages`、
`corner-up-right`、`box`、`quote` 图标。保留原始 24×24 画布和路径，只将
`currentColor` 调整为深灰 `#30353a`。删除复用 `conversation-trash-2.svg`；
所有资源共用本目录的 `LICENSE.txt`。布局位于 `dialogs/messagemenu.ui`。

统一使用 [Lucide](https://github.com/lucide-icons/lucide) 的 SVG 图标。

`update-download.svg` 复用下述固定提交的 `icons/download.svg`，用于标题栏“检查更新”按钮；保留路径和画布，仅将 `currentColor` 改为 `#656a70`，沿用本目录 ISC 许可。

## 来源与许可

- 版本：`0.468.0`
- 固定提交：`f12b0de177fbc2a6795e99be065887e72b237123`
- 原始文件：上述提交的 `icons/<图标名>.svg`
- 原始许可：随附 `LICENSE.txt`，包含 Lucide ISC 和 Feather 来源说明。
- 修改范围：保留原始图形路径与 2px 描边，只调整画布留白和描边颜色。

## 控件与资源对应

| 控件 | 图标 | 用途 |
| --- | --- | --- |
| `side_chat_lb` | `message-circle` | 聊天导航 |
| `side_contact_lb` | `contact-round` | 联系人导航 |
| `side_miniapp_btn` | `blocks` | 收藏入口 |
| `mini_program_btn` | `blocks` | 小程序 |
| `side_discover_btn` | `compass` | 看一看 |
| `side_phone_btn` | `smartphone` | 手机 |
| `side_menu_btn` | `menu` | 导航菜单 |
| `add_btn` | `circle-plus` | 展开快捷操作菜单 |
| `emo_lb` | `smile` | 表情面板 |
| `file_lb` | `folder-open` | 选择图片 |
| `screenshot_btn` | `scissors` | 截图 |
| `voice_btn` | `mic` | 语音消息 |
| `chat_more_btn` | `ellipsis` | 会话设置 |

## 快捷菜单与添加朋友窗口

以下资源同样来自上述固定提交，保留完整的 `24×24` 画布，用于 `QIcon`。
菜单图标不再带导航按钮的额外画布留白，显示为 `22×22`；关闭图标显示为 `18×18`。
所有图标共用本目录的 `LICENSE.txt`。

| 资源 | Lucide 原图 | 用途 |
| --- | --- | --- |
| `action-group-chat.svg` | `users-round.svg` | 发起群聊 |
| `action-add-friend.svg` | `user-round-plus.svg` | 添加朋友 |
| `action-note.svg` | `notebook-pen.svg` | 写笔记 |
| `action-search.svg` | `search.svg` | 添加朋友搜索框 |
| `action-close.svg` | `x.svg` | 添加朋友窗口关闭按钮 |

## 收藏占位页

收藏页继续使用同一 Lucide 固定提交中的原始图形，仅调整描边颜色；页面布局写在
`dialogs/favoritespage.ui` 中，图标通过 `rc.qrc` 编入程序。

| 资源 | Lucide 原图 | 用途 |
| --- | --- | --- |
| `favorites-grid*.svg`、`blocks-selected.svg` | `blocks.svg` | 全部收藏、侧栏选中态 |
| `favorites-clock.svg` | `clock-3.svg` | 最近使用 |
| `favorites-link.svg` | `link-2.svg` | 链接 |
| `favorites-image.svg` | `image.svg` | 图片与视频 |
| `favorites-message.svg` | `message-circle-more.svg` | 聊天记录 |
| `favorites-audio.svg` | `audio-lines.svg` | 语音 |

原始文件可在 [Lucide 固定提交的 icons 目录](https://github.com/lucide-icons/lucide/tree/f12b0de177fbc2a6795e99be065887e72b237123/icons) 中核对。

## 尺寸与状态

- 所有上述控件的点击区域均为 `34×34` Qt 逻辑像素。
- 原始图标使用 `24×24` 坐标；将 SVG 画布扩展为 `viewBox="-5 -5 34 34"`，四周各留 5px 空白。
- QSS 的 `border-image` 铺满按钮时，内部图形仍保持 24px 图标比例，不会因字体或控件类型变化而变大。
- `normal` 为灰色 `#656a70`，`hover` 为深灰 `#30353a`，导航 `selected` 为绿色 `#07c160`。
- `rc.qrc` 将图标编入程序；`style/components/icons.qss` 使用 `:/icons/…` 加载资源。
- ClickedLabel 沿用动态 `state` 属性，QPushButton 沿用 `:hover`、`:pressed` 状态。
- 工具栏高度保持 44px，上下各留 5px，图标间距为 8px。

除已经接入静态占位页的“收藏”入口外，其他标有“暂未实现”的按钮仍保留原有提示与行为。

## 联系人界面

联系人分组和底部操作继续使用同一 Lucide 固定提交，保留 24×24 画布及原始路径。
新增 `contact-chevron-right/down`（chevron-right/down）、`contact-star`（star）、
`contact-chat`（message-circle）、`contact-phone`（phone）、`contact-video`（video）。
分类图标仅调整为灰色，底部操作图标仅调整为蓝色；通讯录管理复用 `action-group-chat.svg`。

## 收藏入口图标修正

侧栏第三个按钮和聊天工具栏入口均为收藏，控件名分别为 `side_favorites_btn` 和
`favorites_btn`，统一使用 `bookmark-normal.svg` / `bookmark-hover.svg`，侧栏选中态
使用 `bookmark-selected.svg`。图形来自上述固定 Lucide 提交的 `icons/bookmark.svg`，
保留原始路径、2px 描边以及 34×34 留白画布，继续适用本目录许可。
两个入口都打开现有收藏页；发现页中的小程序入口仍使用 `Resources/discover/miniapps.svg`。
此说明更新前面表格中的旧控件名与图标映射。

## 聊天设置抽屉

聊天记录入口 `history-entry-normal.svg` / `history-entry-hover.svg` 组合上述固定提交的
`message-circle-more.svg` 与 `chevron-down.svg`，图形路径保持不变，仅调整颜色、位置和箭头缩放，
组合为 `54×34` 点击区域。聊天记录窗口的搜索、语音和空状态图标分别复用
`action-search.svg`、`mic-normal.svg`、`favorites-message.svg`，共用本目录的 `LICENSE.txt`。
来源：https://github.com/lucide-icons/lucide/tree/f12b0de177fbc2a6795e99be065887e72b237123/icons

`settings-plus.svg`、`settings-switch-off.svg`、`settings-switch-on.svg` 基于同一
Lucide 固定提交 `f12b0de177fbc2a6795e99be065887e72b237123` 的 `plus.svg`、
`toggle-left.svg`、`toggle-right.svg`。加号调整灰色描边和线宽；开关改为填色胶囊，
增大白色圆点半径，并裁剪画布空白。箭头复用 `contact-chevron-right.svg`。
源文件位于上述 GitHub icons 目录，授权继续使用本目录 `LICENSE.txt`。
布局在 `dialogs/chatsettingspanel.ui`，样式在 `style/pages/chat.qss`。

## 聊天会话右键菜单

`conversation-*.svg` 来自同一固定 Lucide 提交的 `pin`、`pin-off`、`mail`、
`mail-open`、`bell-off`、`external-link`、`eye-off`、`trash-2`。保留原始 24×24
画布和路径，仅调整灰色描边，删除项使用红色描边。菜单图标显示为 18×18，
会话列表的置顶标记显示为 16×16；共用本目录的 `LICENSE.txt`。
