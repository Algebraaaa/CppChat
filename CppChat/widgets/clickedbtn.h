#ifndef CLICKEDBTN_H
#define CLICKEDBTN_H

#include <QEnterEvent>
#include <QPushButton>

class ClickedBtn : public QPushButton {
  Q_OBJECT
public:
  // explicit 防止 QWidget* 被意外隐式转换成 ClickedBtn
  explicit ClickedBtn(QWidget *parent = nullptr);
  ~ClickedBtn();

  // 保存普通、悬停、按下三种状态名，并让 QSS 根据 state 属性选择对应样式。
  void SetState(const QString &normal, const QString &hover, const QString &press);

protected:
  // override 表明这些函数重写了 Qt 基类中的虚函数，签名写错时编译器会报错。
  void enterEvent(QEnterEvent *event) override;        // 鼠标进入按钮区域
  void leaveEvent(QEvent *event) override;             // 鼠标离开按钮区域
  void mousePressEvent(QMouseEvent *event) override;   // 鼠标按键被按下
  void mouseReleaseEvent(QMouseEvent *event) override; // 鼠标按键被释放

private:
  // 统一完成动态属性修改、QSS 重新匹配和按钮重绘。
  void applyState(const QString &state);

  QString _normal; // 普通状态对应的 QSS state 属性值
  QString _hover;  // 鼠标悬停状态对应的 QSS state 属性值
  QString _press;  // 鼠标按下状态对应的 QSS state 属性值
};

#endif // CLICKEDBTN_H
