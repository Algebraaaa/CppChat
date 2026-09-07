#ifndef CLICKEDLABEL_H
#define CLICKEDLABEL_H

#include "common/global.h"
#include <QLabel>
#include <QString>

// ClickedLabel 继承 QLabel，并补充按钮式的鼠标交互和信号。
class ClickedLabel : public QLabel {
  // 启用 Qt 元对象系统，使自定义信号可以通过 connect() 连接。
  Q_OBJECT

public:
  // parent 用于加入 Qt 对象树；nullptr 表示创建时暂时没有父控件。
  explicit ClickedLabel(QWidget *parent = nullptr);

  // 配置普通、悬停、按下三种 QSS 动态属性值。
  // press 允许为空；为空时按下阶段沿用 hover 样式。
  void SetState(const QString &normal, const QString &hover, const QString &press = "");

protected:
  void mousePressEvent(QMouseEvent *event) override;   // 处理鼠标按下。
  void mouseReleaseEvent(QMouseEvent *event) override; // 处理鼠标释放。
  void enterEvent(QEnterEvent *event) override;        // 处理鼠标进入。
  void leaveEvent(QEvent *event) override;             // 处理鼠标离开。

private:
  // 统一完成动态属性修改、QSS 重新匹配和界面重绘。
  void applyState(const QString &state);

  QString _normal; // 鼠标不在控件上时的 state 值。
  QString _hover;  // 鼠标悬停时的 state 值。
  QString _press;  // 鼠标左键按下时的 state 值。

  // 记录这次左键是否从当前控件开始按下，用于判断释放时能否发出 clicked。
  bool _left_button_pressed = false;

signals:
  void clicked();  // 左键在控件内按下并在控件内释放后发出。
  void pressed();  // 鼠标左键按下时发出。
  void released(); // 鼠标左键释放时发出。
};

#endif // CLICKEDLABEL_H
