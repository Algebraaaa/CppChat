# CppChat 样式维护

## 从哪里改

| 需求 | 文件 |
| --- | --- |
| 修改字体、主题模块加载顺序 | `theme.json` |
| 修改常用颜色、字号、字重、圆角 | `tokens.json` |
| 修改全局字体或文字气泡底层外观 | `base.qss` |
| 修改主按钮、次按钮、验证码按钮、表单输入框 | `components/controls.qss` |
| 修改图标的普通、悬停、按下、选中状态 | `components/icons.qss` |
| 修改聊天和联系人滚动条 | `components/scrollbars.qss` |
| 修改窗口标题栏 | `pages/window.qss` |
| 修改登录、注册、重置密码的页面细节 | `pages/auth.qss` |
| 修改会话列表、聊天标题、输入区、消息状态 | `pages/chat.qss` |
| 修改发现入口列表、右侧占位区域 | `pages/discover.qss` |
| 修改联系人列表、搜索结果、好友申请列表 | `pages/contacts.qss` |
| 修改查找、添加好友、表情、图片查看等弹窗 | `pages/dialogs.qss` |
| 修改个人资料和联系人资料 | `pages/profile.qss` |

当前只提供 `wechat-light` 主题。模块全部属于这一套主题，由清单确定加载顺序。
图标和字体继续放在 `Resources/icons`、`Resources/fonts`，保留上游许可与来源。
界面中英文统一使用内置思源黑体 `Source Han Sans CN`，正文为 Regular（400），
标题和强调为 Medium（500）；字号层级保持不变，Emoji 使用系统表情字体。
版本、下载来源、许可及校验值见 [字体说明](../Resources/fonts/README.md)。

聊天侧栏、快捷操作、聊天工具栏和发送入口统一使用 `QPushButton`，对象名以 `_btn` 结尾。
普通、悬停、按下、选中状态分别用默认规则、`:hover`、`:pressed`、`:checked`，
不再通过 `SetState()` 手动切换这些按钮的样式。聊天和联系人使用 `QButtonGroup`
互斥选中；未读红点是按钮内的 `QLabel`，只显示状态，不参与点击。

## 加载流程

```text
app/main.cpp
    → ThemeManager::apply()
    → theme.json：名称、字体资源、字体回退、模块顺序
    → tokens.json：公共视觉参数
    → 读取各模块并展开 ${参数名}
    → 注册字体
    → QApplication::setFont() / setStyleSheet()
```

`${color.brand}` 是项目模板占位符，Qt 不直接识别这个语法。
`ThemeManager` 在应用样式前将它替换成真实值。参数缺失、JSON 无效、
模块重复、资源缺失或字体注册失败时，会返回明确错误；启动入口记录错误并退出。
模块全部成功读取后才应用到窗口，避免界面只应用一半样式。

公共颜色集中维护；仅某个业务页面使用的特殊颜色保留在对应页面模块。
`font.family` 根据 `theme.json` 中的 `fontFamilies` 自动生成，避免 C++ 和 QSS
维护两份字体回退列表。基础字号由 `font.size.base` 同时提供给应用字体和 QSS。

## 公共控件约定

### Windows 深色模式下的消息气泡

当前客户端保持 `wechat-light` 外观，不根据 Windows 应用模式切换整套界面。
系统使用深色模式或运行中切换浅色/深色模式时，消息气泡仍使用应用主题的完整颜色组合，
不要求用户关闭 Windows 深色模式，也不修改 `QT_QPA_PLATFORM` 或系统设置。

- `color.chat.bubble.self` / `color.chat.bubble.other` 控制发送/接收气泡背景。
  `BubbleFrame` 的 `chatRole` 在首次 polish 前确定，QSS 的 `qproperty-backgroundColor`
  将颜色传给自绘背景与箭头；图片气泡复用同一规则。
- `QTextEdit#chat_message_text` 明确使用 `color.text.body` 绘制正文，
  选中文字使用 `color.text.primary`，选中背景使用 `color.selection`。
  正常、选中和窗口失去焦点后的文字都不依赖系统的深色正文调色板。
- 这是系统深色模式兼容修复，不是新增 `wechat-dark` 主题；不要只修改背景或只让文字跟随系统。

采用 Qt 已有的 [QSS 文字与选中颜色](https://doc.qt.io/qt-6/stylesheet-reference.html#color)
和 [QObject 属性规则](https://doc.qt.io/qt-6/stylesheet-syntax.html#setting-qobject-properties)，
沿用现有主题参数与 `BubbleFrame`，无需引入新依赖；所用接口兼容 Qt 6.5 和 6.12。

静态回归（不编译，Python 标准库）：

```powershell
python -B -m unittest discover -s tests/unit -p test_chat_theme.py -v
```

现有 `client_tests` 增加消息气泡颜色回归，模拟浅色/深色调色板启动和运行中切换，
检查发送/接收、普通/选中、Active/Inactive/Disabled 状态和切换后新建的气泡。
原生测试需要用户自行启用 `CPPCHAT_BUILD_TESTS=ON` 并构建；静态通过不等于桌面渲染验收。
发布前在 Windows 浅色/深色应用模式各检查一次中文、英文、数字、Emoji、双击选择与取消选择，
再在客户端运行时切换应用模式，确认旧消息和新消息都清晰；不要使用 `darkmode=0` 参数验收。

### 窗口外边框

主窗口和独立弹窗的外边框共用 `tokens.json` 中的 `color.border.window`
（默认 `#b8bec6`，1px 浅灰边线）。添加好友、查找结果、好友申请等透明圆角弹窗
由 QSS 绘制边框；不要对透明窗口再叠加系统边框。

Windows 原生外框不能用 QSS 改色。`ThemeManager::apply()` 为应用安装
`WindowFrameFilter`，统一设置主窗口、聊天记录、个人资料、图片预览和 Qt 提示框
的系统外框颜色，原生标题栏背景使用 `color.surface.subtle`，标题文字使用
`color.text.primary`。窗口显示、激活和句柄重建时自动重新应用，保留系统拖动、
缩放、阴影及标题栏按钮行为。Windows 11 Build 22000 起支持这些 DWM 颜色属性；
旧 Windows 不支持时保留系统外观。系统原生文件选择器不属于 Qt QWidget，不在此范围内。

输入框焦点、按钮选中和错误提示的边框仍使用原有业务颜色，不属于窗口外边框。

### 按钮和输入框

控件用途在 `.ui` 中用动态属性声明：

```xml
<property name="buttonRole" stdset="0"><string notr="true">primary</string></property>
<property name="inputRole" stdset="0"><string notr="true">form</string></property>
```

- `buttonRole=primary`：主操作按钮。
- `buttonRole=secondary`：取消、返回、注册入口等次操作。
- `buttonRole=verification`：获取验证码。
- `inputRole=form`：标准表单输入框。

新增同类控件时，在 Designer 中添加用途属性即可复用公共样式。
用途标识不参与翻译（`notr="true"`），放在控件尺寸属性之前，保证首次布局时可匹配。
运行时修改动态属性后，需要调用项目现有 `repolish()` 刷新样式。
ClickedBtn、ClickedLabel 继续使用它们已有的动态 `state`；
Qt 原生按钮继续使用 `:hover`、`:pressed`、`:disabled` 等状态。

## 文件职责

- `.ui`：布局、固定尺寸、最小尺寸、控件用途。图标按钮尺寸统一在这里维护。
- QSS：颜色、字体、边框、圆角、状态外观、图标资源映射。
- C++：点击行为、数据更新、主题加载；业务控件不自行拼接 QSS。

需要特殊外观的控件应限定在所属页面内，避免使用无范围的全局 ID。
新增模块时同时更新 `theme.json` 和 `rc.qrc`；不要依赖文件名排序。
公共样式在前，页面样式在后；发生覆盖时同时检查选择器优先级与模块顺序。
修改某个页面时直接编辑对应模块，不在其他文件末尾追加覆盖补丁。

## 检查与回归

在项目根目录运行：

```powershell
python scripts/check_style.py
```

这个检查只需要 Python 标准库，验证参数、模块、图标和字体的资源路径，
以及 `.ui` 中声明的控件用途是否有对应样式。

布局、字体或选择器的修改，在 Qt Creator 重新构建后，检查登录、注册、
聊天、联系人及资料页面；同时核对悬停、选中、禁用状态和窄窗口布局。

此前用于图片修复和样式迁移的专项测试程序已清理。日常保留上面的资源检查脚本。

## 当前方案的边界

- 使用 Qt 原生 QSS 编写规则，`${...}` 的公共参数替换由项目负责。
  带占位符的源文件不能直接交给 Designer 预览，应通过真实页面查看渲染结果。
- 当前只在启动时加载内置主题；没有实现动态切换、多主题管理或第三方主题导入。
- 拆分粒度按现有公共控件和页面确定。新增规则放到所属模块，有实际复用需求
  再抽取公共样式，避免为单个控件建立额外配置体系。
- 配置错误必须报告具体原因；加载失败不返回半成品主题。

## 2026-10-06 维护入口调整

按用户要求已删除客户端 `scripts/` 和 `tests/`。上面的 `check_style.py` 命令是历史说明，当前源码不再提供此脚本。
维护时核对 `rc.qrc` 的资源路径与 `style/theme.json` 的模块清单，重新运行 CMake 并构建后，按上面的页面和状态清单人工验收。
  检查脚本使用显式校验，Python 优化模式下也会执行检查。
- 更新主题时运行资源检查；涉及布局、字体或选择器时，再检查真实页面。
