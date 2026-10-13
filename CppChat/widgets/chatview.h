#ifndef CHATVIEW_H
#define CHATVIEW_H
#include <QPointer>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QVBoxLayout>

class QGraphicsOpacityEffect;
class QTimer;
class QWheelEvent;

class ChatView : public QWidget {
  Q_OBJECT
public:
  explicit ChatView(QWidget *parent = nullptr);
  void beginUpdate(bool scrollToBottom = false);
  void endUpdate();
  void scrollToBottom();
  void removeAllItem();
  void appendChatItem(QWidget *item);
  void prependChatItem(QWidget *item);
  void insertChatItem(QWidget *before, QWidget *item);
  void setChatItems(const QList<QWidget *> &items);

protected:
  bool eventFilter(QObject *object, QEvent *event) override;
  void paintEvent(QPaintEvent *) override;
  void enterEvent(QEnterEvent *event) override;
  void leaveEvent(QEvent *event) override;

private:
  bool handleWheelEvent(QWheelEvent *event);
  void updateBarVisibility();
  void onVScrollBarMoved(int minimum, int maximum);
  void captureAnchor();
  void restoreAnchor();

  QScrollArea *m_pScrollArea;
  QScrollBar *m_pVScrollBar;
  QPropertyAnimation *m_pScrollAnimation;
  QGraphicsOpacityEffect *_barOpacity;
  QTimer *_layoutTimer;
  int m_scrollTarget = 0;
  int _savedValue = 0;
  QPointer<QWidget> _anchor;
  QString _anchorKey;
  int _anchorOffset = 0;
  bool _followBottom = true;
  bool _batchUpdating = false;
  bool _restoringScroll = false;
};
#endif
