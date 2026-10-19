#ifndef CHATHISTORYDIALOG_H
#define CHATHISTORYDIALOG_H

#include "common/chatstore.h"
#include <QDialog>

class QButtonGroup;
class QTimer;
class QComboBox;
class HistoryExporter;
class QPushButton;
class QLabel;
namespace Ui {
class ChatHistoryDialog;
}

// 独立聊天记录窗口，每页从当前账号本地数据库查询 50 条。
class ChatHistoryDialog : public QDialog {
  Q_OBJECT
public:
  explicit ChatHistoryDialog(QWidget *parent = nullptr);
  ~ChatHistoryDialog() override;
  void SetConversation(int threadId, const QString &name);
  void SetAllConversations();
  void Reset();
signals:
  void openThreadRequested(int threadId);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void done(int result) override;

private:
  void Query(bool reset = true);
  void Display(const ChatStoreResult &result);
  void AppendMessage(const StoredMessage &message);
  void BeginExport();
  void SetExportBusy(bool busy);
  void RefreshScope();
  QVector<int> AllowedThreads() const;
  QString ThreadName(int threadId) const;
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
  bool _global = false;
  QComboBox *_scope;
  HistoryExporter *_exporter;
  QPushButton *_exportButton;
  QLabel *_exportStatus;
};

#endif
