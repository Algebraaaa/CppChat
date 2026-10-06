# CppChat 界面字体

界面文字统一使用 Adobe 的思源黑体简体中文子集版（Source Han Sans CN），
中文、英文和数字使用同一字族。正文使用 Regular（400），标题和强调文字使用
Medium（500），不再混用 Open Sans / 微软雅黑或请求未打包的 600 字重。
这是一套无衬线字体，本项目选择它用于中文聊天界面的日常阅读；舒适度仍需在实际显示器上确认。
Emoji 保留系统 Segoe UI Emoji 字体，以免彩色表情变成普通单色文字。

## 来源与许可

- 官方仓库：https://github.com/adobe-fonts/source-han-sans
- 固定提交：`a4f7cf94edfb9d7ffbdfc4841de276358bd7e0f2`
- 字体版本：`2.005`
- 原始路径：`SubsetOTF/CN/SourceHanSansCN-Regular.otf`、`SourceHanSansCN-Medium.otf`
- 许可：随附未经修改的 `SourceHanSans-LICENSE.txt`，SIL Open Font License 1.1。
  分发时保留版权和许可；字体文件直接使用上游静态 OTF，没有重新子集化、转换或改名。
- 字体及本说明、许可均通过 `rc.qrc` 打包，启动时由 `app/thememanager.cpp` 注册，
  用户无需在操作系统中安装字体，也无需联网下载。

## 文件校验

| 文件 | 字节数 | SHA-256 |
| --- | --- | --- |
| `SourceHanSansCN-Regular.otf` | 8429224 | `e2bc8a2e7f37474b774fff8db758681ece40bb6947a90d571bce9dd60671a8e4` |
| `SourceHanSansCN-Medium.otf` | 8406556 | `a94e558a2fe972bee4f46bce0843abff37063fd68c33f1e7d9058f6f09432b01` |

两个原始字体合计约 16.1 MiB。使用官方中文子集，而不是包含所有地区字形的完整集合。
静态检查确认两个字体的内部字族、400/500 字重及当前 `.ui` 文案字符覆盖；
这不等同于 Windows/Qt 的实际字体匹配和视觉验证。

## 维护位置

字族和回退顺序仅在 `style/theme.json` 中维护，字号与字重在 `style/tokens.json` 中维护。
所有页面从应用默认字体和 `style/base.qss` 继承，不在 `.ui` 或业务 C++ 中指定另一套字体。
基础字号为 14px，聊天正文和输入框为 15px，联系人名称为 15px，聊天时间为 11px。
这些数值是 Qt 的逻辑像素，会随系统显示缩放调整。
特殊字符和未覆盖的多语言字符仍可回退到系统字体；未打包或复制 Windows 系统字体。

旧的 `OpenSans-Regular.ttf`、`OpenSans-SemiBold.ttf` 和 `OFL.txt` 暂留作历史素材，
不再注册、打包或参与字族回退。旧字体来源为 https://github.com/googlefonts/opensans，
固定提交 `bd7e37632246368c60fdcbd374dbf9bad11969b6`，原始路径为 `fonts/ttf/`。
