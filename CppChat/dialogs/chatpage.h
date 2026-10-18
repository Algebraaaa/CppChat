#ifndef CHATPAGE_H
#define CHATPAGE_H
#include "common/userdata.h"
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QWidget>
class ChatItemBase;
class QTimer;
class ScreenCapture;
class ChatHistoryDialog;
class MessageSender;
class VoiceCallDialog;
namespace Ui {
class ChatPage;
}
class ChatPage : public QWidget {
  Q_OBJECT
public:
  explicit ChatPage(QWidget *parent = nullptr);
  ~ChatPage() override;
  void SetChatData(std::shared_ptr<ChatThreadData> data);
  void UpdateChatStatus(const QString &uniqueId, int status);
  void ShowStatus(const QString &message);
  void Reset();
  void RemoveChatThread(int threadId);
  void SaveCurrentDraft();
  void SetThreadPinned(int threadId, bool pinned);
signals:
  void sig_group_settings(int threadId);
  void sig_reload_local(int threadId);
  void sig_messages_changed(int threadId);
  void sig_open_favorites();
  void sig_thread_pin_changed(int threadId, bool pinned);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void paintEvent(QPaintEvent *) override;
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  void RenderMessages(bool changed);
  QString SenderName(int uid) const;
  ChatItemBase *CreateChatItem(const std::shared_ptr<ChatDataBase> &message);
  void UpdateSettingsRect();
  void OpenHistory();
  void StartScreenCapture();
  void on_send_btn_clicked();
  void UpdateSendButton();
  void PersistDraft(int threadId);
  MessageSender *_sender = nullptr;
  VoiceCallDialog *_voice = nullptr;
  Ui::ChatPage *ui;
  QTimer *_draftTimer = nullptr;
  bool _restoringDraft = false;
  QSet<int> _loadingDrafts;
  QPointer<ScreenCapture> _screenCapture;
  ChatHistoryDialog *_historyDialog = nullptr;
  std::shared_ptr<ChatThreadData> _chat_data;
  QHash<QString, ChatItemBase *> _pendingItems;
  QHash<QString, ChatItemBase *> _renderedItems;
  QHash<QString, std::shared_ptr<ChatDataBase>> _renderedMessages;
  QHash<QString, QWidget *> _auxiliaryRows;
  QHash<int, QVector<MsgInfo>> _drafts;
  QSet<int> _dirtyDrafts;
};
#endif
