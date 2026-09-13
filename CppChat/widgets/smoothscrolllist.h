#ifndef SMOOTHSCROLLLIST_H
#define SMOOTHSCROLLLIST_H

#include <QListWidget>

class QEnterEvent;
class QEvent;
class QPropertyAnimation;
class QResizeEvent;
class QScrollBar;
class QWheelEvent;

// 为 QListWidget 提供悬浮滚动条和平滑滚轮动画。
// 业务列表只需要继承该类，并按需重写 onReachedBottom()。
class SmoothScrollList : public QListWidget {
public:
  explicit SmoothScrollList(QWidget *parent = nullptr);

protected:
  void enterEvent(QEnterEvent *event) override;
  void leaveEvent(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;

  virtual void onReachedBottom();

private:
  void updateScrollBarGeometry();
  void updateScrollBarVisibility();

  QScrollBar *_overlayScrollBar = nullptr;
  QPropertyAnimation *_scrollAnimation = nullptr;
  int _scrollTarget = 0;
};

#endif // SMOOTHSCROLLLIST_H
