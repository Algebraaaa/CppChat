#ifndef QUICKACTIONMENU_H
#define QUICKACTIONMENU_H

#include <QWidget>

class QKeyEvent;
namespace Ui { class QuickActionMenu; }

class QuickActionMenu : public QWidget
{
  Q_OBJECT
public:
  explicit QuickActionMenu(QWidget *parent = nullptr);
  ~QuickActionMenu() override;
  void Popup(const QPoint &position);

signals:
  void addFriendRequested();
  void placeholderRequested(const QString &title);

protected:
  void keyPressEvent(QKeyEvent *event) override;

private:
  Ui::QuickActionMenu *ui;
};

#endif
