#ifndef CONVERSATIONMENU_H
#define CONVERSATIONMENU_H

#include <QWidget>
class QKeyEvent;
namespace Ui {
class ConversationMenu;
}

// Qt::Popup 自动处理外部点击关闭；尺寸、菜单项和分隔线由 .ui 定义。
class ConversationMenu : public QWidget
{
  Q_OBJECT
public:
  explicit ConversationMenu(QWidget *parent = nullptr);
  ~ConversationMenu() override;
  void Popup(int threadId, bool pinned, bool unread, const QPoint &position);

signals:
  void sig_pin_changed(int threadId, bool pinned);
  void sig_unread_changed(int threadId, bool unread);
  void sig_placeholder(const QString &title);

protected:
  void keyPressEvent(QKeyEvent *event) override;

private:
  Ui::ConversationMenu *ui;
  int _threadId = 0;
  bool _pinned = false;
  bool _unread = false;
};
#endif
