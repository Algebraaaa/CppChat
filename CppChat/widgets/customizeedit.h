#ifndef CUSTOMIZEEDIT_H
#define CUSTOMIZEEDIT_H

// QLineEdit 是 Qt 的单行文本输入框。
#include <QLineEdit>

// CustomizeEdit 在普通输入框上增加安全字数限制和失去焦点通知。
class CustomizeEdit : public QLineEdit {
  // 启用 Qt 元对象系统，从而可以声明并发出 sig_foucus_out 信号。
  Q_OBJECT
public:
  // parent 用于把输入框挂到 Qt 父子对象树中。
  explicit CustomizeEdit(QWidget *parent = nullptr);

  // 改前：maxLen 表示 UTF-8 最大字节数，中文和 emoji 会占用多个字节。
  // 改后：maxLen 表示用户最多可以输入多少个完整可见字符；小于等于 0 表示不限制。
  void SetMaxLength(int maxLen);

protected:
  // 重写焦点移出事件，以便在保留 QLineEdit 原行为后发出自定义通知。
  void focusOutEvent(QFocusEvent *event) override;

private:
  // 检查用户新输入的文本，超过限制时只保留完整的 Unicode 字素。
  void limitTextLength(const QString &text);

  // 改前代码（仅保留作对照，不参与编译）：int _max_len;
  int _max_grapheme_count; // 允许输入的最大完整可见字符数，0 表示不限制。

signals:
  // 输入框失去焦点后发出的自定义信号；foucus 是项目现有拼写，调用时必须保持一致。
  void sig_foucus_out();
};

#endif // CUSTOMIZEEDIT_H
