// 引入 CustomizeEdit 类的声明。
#include "customizeedit.h"
// QTextBoundaryFinder 可以按照 Unicode 规则查找完整字素边界。
#include <QTextBoundaryFinder>

// 构造 QLineEdit 基类，并把最大字素数初始化为 0（不限制）。
CustomizeEdit::CustomizeEdit(QWidget *parent)
    : QLineEdit(parent), _max_grapheme_count(0)
{
  // 改前：监听 textChanged，用户输入和程序调用 setText() 都会触发限制逻辑，
  //       limitTextLength() 内再次调用 setText() 时还可能重复进入该逻辑。
  // 改前代码（仅保留作对照，不参与编译）：
  // connect(this, &QLineEdit::textChanged, this, &CustomizeEdit::limitTextLength);
  //
  // 改后：监听 textEdited，只处理用户在搜索框中的输入。
  // 好处：limitTextLength() 调用 setText() 截断文本时不会递归，也不会误改程序设置的文本。
  connect(this, &QLineEdit::textEdited, this, &CustomizeEdit::limitTextLength);
}

// 更新搜索框允许输入的最大完整可见字符数。
void CustomizeEdit::SetMaxLength(int maxLen)
{
  // 改前代码（仅保留作对照，不参与编译）：
  // _max_len = maxLen;
  //
  // 改后：成员变量保存的是完整字素数量，不再保存 UTF-8 字节数量。
  // 保存新的限制值；小于等于 0 时 limitTextLength() 会把它视为不限制。
  _max_grapheme_count = maxLen;
}

// 当输入框失去键盘焦点时，Qt 会调用这个重写函数。
void CustomizeEdit::focusOutEvent(QFocusEvent *event)
{
  // 先执行 QLineEdit 原本的焦点移出处理，保留 Qt 默认行为。
  QLineEdit::focusOutEvent(event);
  // 再通知外部：这个自定义输入框已经失去焦点。
  emit sig_foucus_out();
}

// 用户编辑文本后检查长度；text 是本次编辑后的完整内容。
void CustomizeEdit::limitTextLength(const QString &text)
{
  // 0 或负数代表不启用字符数量限制，直接保留原文本。
  if (_max_grapheme_count <= 0) {
    return;
  }

  // 改前：先调用 toUtf8() 转成字节数组，再用 left(maxLen) 按字节截断。
  //       截断位置可能落在中文或 emoji 的编码中间，转换回 QString 时会出现乱码或“�”。
  // 改前代码（仅保留作对照，不参与编译）：
  // QByteArray byteArray = text.toUtf8();
  // if (byteArray.size() <= _max_len) {
  //   return;
  // }
  // const qsizetype originalBytes = byteArray.size();
  // byteArray = byteArray.left(_max_len);
  // setText(QString::fromUtf8(byteArray));
  //
  // 改后：使用 Unicode 字素边界查找器，按完整可见字符寻找安全的截断位置。
  // 好处：中文、普通 emoji 和“家庭 emoji”等组合字符都不会被从中间切开。
  // Grapheme 表示按用户看到的完整字符划分，而不是按 UTF-16 单元或 UTF-8 字节划分。
  QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
  // 将查找器移动到字符串开头，准备从第一个字素开始计数。
  finder.toStart();

  // 保存允许保留的最后一个安全边界位置。
  qsizetype cutPosition = 0;
  // 保存目前已经经过了多少个完整字素。
  int graphemeCount = 0;

  // 最多向后查找 _max_grapheme_count 个完整字素。
  while (graphemeCount < _max_grapheme_count) {
    // 移动到下一个字素边界，并取得它在 QString 中的位置。
    const qsizetype nextPosition = finder.toNextBoundary();
    // -1 表示已经到达文本结尾，说明输入没有超过限制。
    if (nextPosition == -1) {
      return;
    }

    // 记录当前完整字素之后的安全截断位置。
    cutPosition = nextPosition;
    // 完整字素计数加一。
    ++graphemeCount;
  }

  // 找不到下一个边界，说明文本刚好没有超过限制。
  if (finder.toNextBoundary() == -1) {
    return;
  }

  // 保存截断前的光标位置，避免 setText() 后光标无条件跳到末尾。
  const int oldCursorPosition = cursorPosition();
  // left(cutPosition) 只截取到已经确认安全的完整字素边界。
  const QString limitedText = text.left(cutPosition);
  // 用安全截断后的内容替换搜索框文本；setText() 不会发出 textEdited。
  setText(limitedText);
  // 恢复光标位置；如果旧位置超过新文本末尾，则把光标放到末尾。
  setCursorPosition(qMin(oldCursorPosition, static_cast<int>(limitedText.size())));
}
