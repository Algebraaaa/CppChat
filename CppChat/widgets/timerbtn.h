#ifndef TIMERBTN_H
#define TIMERBTN_H

#include <QPushButton>
#include <QTimer>

// TimerBtn 在普通按钮上增加“获取验证码后倒计时”的能力
class TimerBtn : public QPushButton {
public:
  // explicit 避免 QWidget* 到 TimerBtn 的意外隐式转换。
  explicit TimerBtn(QWidget *parent = nullptr);
  ~TimerBtn() override = default;

  // 启动倒计时；不传参数时默认倒计时 10 秒。
  void startCountdown(int seconds = 10);
  // 停止倒计时，并把按钮恢复成可再次点击的初始状态。
  void resetCountdown();

private:
  QTimer *_timer;  // 指向负责每秒通知一次的定时器对象。
  int _counter = 0; // 保存当前还剩多少秒。
};

#endif // TIMERBTN_H
