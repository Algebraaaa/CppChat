#ifndef CHATPAGE_H
#define CHATPAGE_H
#include "common/userdata.h"
#include <QWidget>
#include <QHash>
class ChatItemBase;
class QLabel;
namespace Ui { class ChatPage; }
class ChatPage : public QWidget {
  Q_OBJECT
public:
  explicit ChatPage(QWidget *parent = nullptr);
  ~ChatPage() override;
  void SetChatData(std::shared_ptr<ChatThreadData> data);
  void AppendChatMsg(std::shared_ptr<ChatDataBase> message);
  void UpdateChatStatus(const QString &uniqueId, int status);
  void ShowStatus(const QString &message);
  void Reset();
signals:
  void sig_messages_changed(int threadId);
protected:
  void paintEvent(QPaintEvent *) override;
private:
  void on_send_btn_clicked();
  Ui::ChatPage *ui;
  QLabel *_statusLabel = nullptr;
  std::shared_ptr<ChatThreadData> _chat_data;
  QHash<QString, ChatItemBase *> _pendingItems;
  QHash<int, QString> _drafts;
};
#endif
