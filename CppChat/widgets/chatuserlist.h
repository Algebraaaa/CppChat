#ifndef CHATUSERLIST_H
#define CHATUSERLIST_H

#include <QEnterEvent>
#include <QListWidget>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>

// ChatUserList 在 QListWidget 上增加覆盖式滚动条和滚动到底部通知。
class ChatUserList : public QListWidget {
  Q_OBJECT
public:
  ChatUserList(QWidget *parent = nullptr);

protected:
  void enterEvent(QEnterEvent *event) override;
  void leaveEvent(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;

signals:
  void sig_loading_chat_user();

private:
  void updateBarGeometry();
  void updateBarVisibility();

  QScrollBar *_overlayScrollBar = nullptr;
  QPropertyAnimation *_scrollAnimation = nullptr;
  int _scrollTarget = 0;
};

#endif // CHATUSERLIST_H
