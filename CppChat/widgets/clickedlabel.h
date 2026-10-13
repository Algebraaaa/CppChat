#ifndef CLICKEDLABEL_H
#define CLICKEDLABEL_H

#include "common/global.h"
#include <QLabel>
#include <QString>

class ClickedLabel : public QLabel {
  Q_OBJECT

public:
  explicit ClickedLabel(QWidget *parent = nullptr);

  // 配置未选中和已选中时各自的普通、悬停、按下状态名。
  // press 允许为空；为空时按下阶段沿用 hover 样式。
  void SetState(const QString &normal, const QString &hover, const QString &press,
                const QString &select, const QString &select_hover, const QString &select_press);
  ClickLbState GetCurState();
  bool SetCurState(ClickLbState state);
  void ResetNormalState();

protected:
  void mousePressEvent(QMouseEvent *event) override;   // 处理鼠标按下
  void mouseReleaseEvent(QMouseEvent *event) override; // 处理鼠标释放
  void enterEvent(QEnterEvent *event) override;        // 处理鼠标进入
  void leaveEvent(QEvent *event) override;             // 处理鼠标离开

private:
  // 统一完成动态属性修改、QSS 重新匹配和界面重绘。
  void applyState(const QString &state);
  QString _normal;         // 鼠标不在控件上时的 state 值
  QString _hover;          // 鼠标悬停时的 state 值
  QString _press;          // 鼠标左键按下时的 state 值
  QString _selected;       // 是否被选中
  QString _selected_hover; // 选中时鼠标悬浮
  QString _selected_press; // 选中时鼠标按压
  ClickLbState _curstate = ClickLbState::Normal;
  // 记录这次左键是否从当前控件开始按下，用于判断释放时能否发出 clicked。
  bool _left_button_pressed = false;

signals:
  void clicked(QString, ClickLbState); // 左键在控件内按下并在控件内释放后发出。
  void pressed();                      // 鼠标左键按下时发出。
  void released();                     // 鼠标左键释放时发出。
};

#endif // CLICKEDLABEL_H
