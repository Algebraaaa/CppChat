#ifndef CHATPAGE_H
#define CHATPAGE_H
#include "common/favorite.h"
#include "common/userdata.h"
#include <QHash>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <QWidget>
class ChatItemBase;
class QTimer;
class ScreenCapture;
class ChatHistoryDialog;
class MessageSender;
class VoiceCallDialog;
class MessageActions;
class QLabel;
class QPushButton;
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
  void CallContact(int uid, bool video);
  bool ForwardSnapshots(const QVector<StoredFavorite> &snapshots,
                        const std::shared_ptr<ChatThreadData> &target);
signals:
  void sig_group_settings(int threadId);
  void sig_reload_local(int threadId);
  void sig_messages_changed(int threadId);
  void sig_open_favorites();
  void sig_thread_pin_changed(int threadId, bool pinned);
  void sig_forward_messages(QVector<StoredFavorite> snapshots);

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
  void SetupMediaControls();
  void StartCall(bool video);
  void UpdateMediaControls();
  void StartScreenCapture();
  void on_send_btn_clicked();
  void UpdateSendButton();
  void PersistDraft(int threadId);
  void SetupSelection();
  void BeginSelection(int serverId);
  void EndSelection();
  void UpdateSelection();
  QVector<std::shared_ptr<ChatDataBase>> SelectedMessages() const;
  QWidget *_selectionBar = nullptr;
  QLabel *_selectionCount = nullptr;
  QList<QPushButton *> _selectionActions;
  QMap<int, std::shared_ptr<ChatDataBase>> _selected;
  bool _selecting = false;
  MessageSender *_sender = nullptr;
  MessageActions *_messageActions = nullptr;
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
