#ifndef BUBBLE_H
#define BUBBLE_H

#include "common/global.h"
#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
class BubbleFrame : public QFrame {
  Q_OBJECT
  Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor)
public:
  BubbleFrame(ChatRole role, QWidget *parent = nullptr);
  QColor backgroundColor() const;
  void setBackgroundColor(const QColor &color);
  void setMargin(int margin);
  void setWidget(QWidget *w);

signals:
  void messageContextMenuRequested(const QPoint &globalPosition);

protected:
  void paintEvent(QPaintEvent *e) override;

private:
  QHBoxLayout *m_pHLayout;
  ChatRole m_role;
  int m_margin;
  QColor m_backgroundColor;
};

#endif // BUBBLE_H
