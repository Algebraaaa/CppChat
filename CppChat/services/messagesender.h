#pragma once

#include "common/userdata.h"
#include <QObject>
#include <QHash>
#include <QQueue>
#include <QSet>

// GUI-thread application service: persist -> upload if needed -> send -> confirm/timeout.
// No widgets, editor state or navigation are owned here.
class MessageSender : public QObject
{
  Q_OBJECT
public:
  explicit MessageSender(QObject *parent = nullptr);
  bool Send(const QVector<MsgInfo> &inputs, const std::shared_ptr<ChatThreadData> &data);
  void RetryMessage(const std::shared_ptr<TextChatData> &message,
                    const std::shared_ptr<ChatThreadData> &data);
  void Reset();
  void RemoveThread(int threadId);
  bool SavingMessages() const
  {
    return _saving;
  }
  bool CanRetry() const;
  static QStringList SplitText(const QString &text, const std::shared_ptr<ChatThreadData> &data);

signals:
  void savingChanged(bool saving);
  void outgoingSaved(int threadId);
  void messagesChanged(int threadId, bool scrollToBottom);
  void messageStatusChanged(int threadId, const QString &uniqueId, int status);
  void status(int threadId, const QString &message);

private:
  struct OutgoingMessage
  {
    MsgInfo content;
    std::shared_ptr<ChatThreadData> chat;
    std::shared_ptr<TextChatData> message;
  };
  void SendNextMessage();
  void SendStoredMessage(const std::shared_ptr<TextChatData> &message,
                         const std::shared_ptr<ChatThreadData> &data);
  void MarkOutgoingFailed(const std::shared_ptr<ChatDataBase> &message,
                          const std::shared_ptr<ChatThreadData> &data, bool definitive = false);
  void FailPending(int threadId, const QString &message, bool definitive);
  bool IsCurrent(const std::shared_ptr<ChatThreadData> &data) const;
  quint64 _revision = 0;
  quint64 _nextAttempt = 0;
  bool _saving = false;
  bool _uploadCommitting = false;
  QSet<QString> _retryingIds;
  QHash<QString, quint64> _attempts;
  QHash<QString, OutgoingMessage> _imageUploads;
  QQueue<OutgoingMessage> _sendQueue;
};
