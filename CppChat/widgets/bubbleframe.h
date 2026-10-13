#ifndef BUBBLE_H
#define BUBBLE_H

#include "common/global.h"
#include <QFrame>
#include <QHBoxLayout>
class BubbleFrame : public QFrame {
  Q_OBJECT
public:
  BubbleFrame(ChatRole role, QWidget *parent = nullptr);
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
};

#endif // BUBBLE_H
