#ifndef CONVERSATIONMENU_H
#define CONVERSATIONMENU_H

#include <QWidget>
class QKeyEvent;
namespace Ui {
class ConversationMenu;
}

// Qt::Popup 自动处理外部点击关闭；尺寸、菜单项和分隔线由 .ui 定义。
class ConversationMenu : public QWidget {
  Q_OBJECT
public:
  explicit ConversationMenu(QWidget *parent = nullptr);
  ~ConversationMenu() override;
  void Popup(int threadId, bool pinned, bool unread, const QPoint &position, bool muted = false);

signals:
  void sig_pin_changed(int threadId, bool pinned);
  void sig_unread_changed(int threadId, bool unread);
  void sig_mute_changed(int threadId, bool muted);
  void sig_hide_requested(int threadId);
  void sig_delete_requested(int threadId);
  void sig_window_requested(int threadId);
  void sig_placeholder(const QString &title);

protected:
  void keyPressEvent(QKeyEvent *event) override;

private:
  Ui::ConversationMenu *ui;
  int _threadId = 0;
  bool _pinned = false;
  bool _unread = false;
  bool _muted = false;
};
#endif
