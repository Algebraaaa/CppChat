#ifndef CHATHISTORYDIALOG_H
#define CHATHISTORYDIALOG_H

#include <QDialog>
#include "common/chatstore.h"

class QButtonGroup;
class QTimer;
namespace Ui {
class ChatHistoryDialog;
}

// 独立聊天记录窗口，每页从当前账号本地数据库查询 50 条。
class ChatHistoryDialog : public QDialog
{
  Q_OBJECT
public:
  explicit ChatHistoryDialog(QWidget *parent = nullptr);
  ~ChatHistoryDialog() override;
  void SetConversation(int threadId, const QString &name);
  void Reset();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void Query(bool reset = true);
  QTimer *_searchTimer;
  quint64 _revision = 0;
  int _pageIndex = 0;
  QVector<HistoryCursor> _pageCursors{HistoryCursor{}};
  HistoryCursor _nextCursor;
  int _type = -1;
  QString _date;
  Ui::ChatHistoryDialog *ui;
  QButtonGroup *_categories;
  int _threadId = 0;
};

#endif
