#include "widgets/timerbtn.h"

TimerBtn::TimerBtn(QWidget *parent) : QPushButton(parent)
{
  // 创建定时器，并把当前按钮 this 设为父对象；按钮销毁时定时器会自动销毁。
  _timer = new QTimer(this);

  // 每当定时器发出 timeout 信号，就执行后面的 lambda 表达式。
  // [this] 捕获当前按钮指针，使 lambda 可以访问 _counter、setText() 等成员。
  connect(_timer, &QTimer::timeout, this, [this]() {
    // 剩余 1 秒或更少时，不再显示 0，而是直接恢复按钮。
    if (_counter <= 1) {
      // 停止定时器、恢复文字并重新启用按钮。
      resetCountdown();
      // 提前结束本次 lambda，避免继续执行下面的减一操作。
      return;
    }
    // 前置 --：先把剩余秒数减一。
    --_counter;
    // 把整数转换成 QString，再显示到按钮上。
    setText(QString::number(_counter));
  });
}

// 启动指定秒数的倒计时。
void TimerBtn::startCountdown(int seconds)
{
  // 已在倒计时，或者参数不是正数时，忽略本次启动请求。
  if (_timer->isActive() || seconds <= 0) {
    return;
  }

  // 保存初始剩余秒数。
  _counter = seconds;
  // 禁用按钮，防止倒计时期间被重复点击。
  setEnabled(false);
  // 立即显示初始秒数，不必等待第一次 timeout。
  setText(QString::number(_counter));
  // 以 1000 毫秒为周期启动定时器，也就是每秒触发一次 timeout。
  _timer->start(1000);
}

// 无论倒计时自然结束还是外部主动调用，都恢复按钮初始状态。
void TimerBtn::resetCountdown()
{
  // 停止继续产生 timeout 信号。
  _timer->stop();
  // 清空内部剩余秒数。
  _counter = 0;
  // tr() 让“获取”文本进入 Qt 翻译系统，方便以后做多语言。
  setText(tr("获取"));
  // 重新启用按钮，允许用户再次点击。
  setEnabled(true);
}
