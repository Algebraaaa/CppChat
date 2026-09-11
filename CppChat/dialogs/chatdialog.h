#ifndef CHATDIALOG_H
#define CHATDIALOG_H
#include "common/global.h"
#include "widgets/statewidget.h"
#include <QDialog>

namespace Ui {
class ChatDialog;
}

class ChatDialog : public QDialog {
  Q_OBJECT

public:
  explicit ChatDialog(QWidget *parent = nullptr);
  ~ChatDialog();
  void addChatUserList();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void ShowSearch(bool bsearch);
  void ClearLabelState(StateWidget *lb);
  void AddLBGroup(StateWidget *lb);
  void handleGlobalMousePress(QMouseEvent *event);
  Ui::ChatDialog *ui;
  ChatUIMode _mode;
  ChatUIMode _state;
  bool _b_loading;
  QList<StateWidget *> _lb_list;
private slots:
  void slot_loading_chat_user();
};

#endif // CHATDIALOG_H
