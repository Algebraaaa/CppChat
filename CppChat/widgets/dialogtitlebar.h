#ifndef DIALOGTITLEBAR_H
#define DIALOGTITLEBAR_H

#include <QPoint>
#include <QWidget>

// 可以在 Designer 中作为 QWidget 的提升控件使用。
// 在标题栏上按住左键，移动它所在的顶层窗口。
class DialogTitleBar : public QWidget {
  Q_OBJECT

public:
  explicit DialogTitleBar(QWidget *parent = nullptr);

protected:
  bool event(QEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  bool _dragging = false;
  QPoint _dragOffset;
};

#endif // DIALOGTITLEBAR_H
