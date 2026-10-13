#ifndef SIDEMENUPOPUP_H
#define SIDEMENUPOPUP_H

#include <QDialog>
#include <QList>

class QPushButton;
namespace Ui {
class SideMenuPopup;
}

// 全局工具入口；不属于四个业务模块，也不直接执行业务操作。
class SideMenuPopup : public QDialog {
  Q_OBJECT
public:
  explicit SideMenuPopup(QWidget *parent = nullptr);
  ~SideMenuPopup() override;
  void PopupAt(QWidget *anchor);

signals:
  void sig_action_requested(const QString &title);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  Ui::SideMenuPopup *ui;
  QList<QPushButton *> _buttons;
};

#endif
