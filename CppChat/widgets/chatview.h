#ifndef CHATVIEW_H
#define CHATVIEW_H
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QVBoxLayout>

class QWheelEvent;

class ChatView : public QWidget {
  Q_OBJECT
public:
  ChatView(QWidget *parent = Q_NULLPTR);
  void appendChatItem(QWidget *item);                  // 尾插
  void prependChatItem(QWidget *item);                 // 头插
  void insertChatItem(QWidget *before, QWidget *item); // 中间插
protected:
  bool eventFilter(QObject *o, QEvent *e) override;
  void paintEvent(QPaintEvent *) override;
  void enterEvent(QEnterEvent *event) override;
  void leaveEvent(QEvent *event) override;
private slots:
  void onVScrollBarMoved(int min, int max);

private:
  bool handleWheelEvent(QWheelEvent *event);
  void updateBarVisibility();

  QScrollArea *m_pScrollArea;
  QScrollBar *m_pVScrollBar = nullptr;
  QPropertyAnimation *m_pScrollAnimation;
  int m_scrollTarget;
  bool isAppended;
};

#endif // CHATVIEW_H
