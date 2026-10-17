#include "messagesender.h"
#include "common/chatstore.h"
#include "common/emojicatalog.h"
#include "network/resourcemgr.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <QUuid>

namespace {
// 回包可能重复正文并将中文转为 \uXXXX；沿用客户端的保守分包预算。
// 服务端目前允许更大报文，这里仍为两份正文和其他字段预留空间。
int escapedJsonSize(const QString &text)
{
  int size = 0;
  for (const auto ch : text) {
    if (ch.unicode() < 0x20 || ch.unicode() > 0x7e)
      size += 6;
    else if (ch == QLatin1Char('"') || ch == QLatin1Char('\\'))
      size += 2;
    else
      ++size;
  }
  return size;
}

QByteArray textPacket(const QString &text, const QString &uniqueId, int sender, int receiver,
                      int thread, int messageType = 0)
{
  const QJsonArray messages{
      QJsonObject{{"content", text}, {"unique_id", uniqueId}, {"msg_type", messageType}}};
  return QJsonDocument(QJsonObject{{"fromuid", sender},
                                   {"touid", receiver},
                                   {"thread_id", thread},
                                   {"text_array", messages}})
      .toJson(QJsonDocument::Compact);
}

} // namespace

MessageSender::MessageSender(QObject *parent) : QObject(parent)
{
  connect(ResourceMgr::instance(), &ResourceMgr::uploadProgress, this,
          [this](const QString &id, qint64 done, qint64 total) {
            if (const auto data = _imageUploads.value(id).chat)
              emit status(data->GetThreadId(),
                          tr("图片上传中：%1/%2 KB").arg(done / 1024).arg(total / 1024));
          });
  connect(ResourceMgr::instance(), &ResourceMgr::uploadFinished, this,
          [this](const QString &requestId, const QString &fileId, const QString &error) {
            const auto upload = _imageUploads.take(requestId);
            const auto data = upload.chat;
            if (!data)
              return;
            if (!error.isEmpty() || !TcpMgr::GetInstance()->IsConnected() ||
                UserMgr::GetInstance()->GetChatThreadByThreadId(data->GetThreadId()) != data) {
              MarkOutgoingFailed(upload.message, data);
              emit status(data->GetThreadId(),
                          error.isEmpty() ? tr("会话已失效，图片保留在本机待确认记录中")
                                          : tr("图片上传失败：%1；图片保留在本机").arg(error));
              SendNextMessage();
              return;
            }
            upload.message->SetContent(fileId);
            _uploadCommitting = true;
            const QPointer<MessageSender> guard(this);
            const auto revision = _revision;
            ChatStore::instance()->SaveMessages(
                {StoredMessage::From(upload.message)}, 0, -1,
                [guard, revision, upload, data](const ChatStoreResult &result) {
                  if (!guard || guard->_revision != revision)
                    return;
                  guard->_uploadCommitting = false;
                  if (!guard->IsCurrent(data)) {
                    guard->SendNextMessage();
                    return;
                  }
                  if (result.error.isEmpty())
                    guard->SendStoredMessage(upload.message, data);
                  else
                    guard->MarkOutgoingFailed(upload.message, data);
                  emit guard->messagesChanged(data->GetThreadId(), false);
                  guard->SendNextMessage();
                });
          });

  const auto tcp = TcpMgr::GetInstance();
  connect(tcp.get(), &TcpMgr::sig_chat_msg_failed, this,
          [this](int threadId, const QString &message) { FailPending(threadId, message, true); });
  connect(tcp.get(), &TcpMgr::sig_request_failed, this,
          [this](ReqId request, const QString &message) {
            if (request == ID_TEXT_CHAT_MSG_REQ)
              FailPending(0, message, false);
          });
}

bool MessageSender::IsCurrent(const std::shared_ptr<ChatThreadData> &data) const
{
  return data && UserMgr::GetInstance()->GetChatThreadByThreadId(data->GetThreadId()) == data;
}

bool MessageSender::CanRetry() const
{
  return TcpMgr::GetInstance()->SupportsMessageDedup() && TcpMgr::GetInstance()->IsConnected();
}

void MessageSender::Reset()
{
  ++_revision;
  _saving = _uploadCommitting = false;
  _retryingIds.clear();
  _attempts.clear();
  _imageUploads.clear();
  _sendQueue.clear();
  emit savingChanged(false);
}

void MessageSender::RemoveThread(int threadId)
{
  for (auto it = _imageUploads.begin(); it != _imageUploads.end();)
    if (it.value().chat->GetThreadId() == threadId)
      it = _imageUploads.erase(it);
    else
      ++it;
  for (auto it = _sendQueue.begin(); it != _sendQueue.end();)
    if (it->chat->GetThreadId() == threadId)
      it = _sendQueue.erase(it);
    else
      ++it;
  SendNextMessage();
}

void MessageSender::FailPending(int threadId, const QString &message, bool definitive)
{
  for (const auto &data : UserMgr::GetInstance()->GetChatThreads()) {
    if (!data || (threadId > 0 && data->GetThreadId() != threadId))
      continue;
    for (const auto &pending : data->GetMsgUnRspRef())
      MarkOutgoingFailed(pending, data, definitive);
    emit status(data->GetThreadId(), message);
  }
}

bool MessageSender::Send(const QVector<MsgInfo> &inputs,
                         const std::shared_ptr<ChatThreadData> &data)
{
  if (_saving || !IsCurrent(data))
    return false;
  if (!ChatStore::instance()->IsReady()) {
    emit status(data->GetThreadId(), tr("本地聊天记录尚未就绪，请检查磁盘后重试"));
    return false;
  }
  if (!TcpMgr::GetInstance()->IsConnected()) {
    emit status(data->GetThreadId(), tr("连接已断开，草稿已保存到本机，请重新登录"));
    return false;
  }
  if ((!data->IsGroup() && !UserMgr::GetInstance()->CheckFriendById(data->GetOtherId()))) {
    emit status(data->GetThreadId(), errorCodeMessage(FriendNotFound));
    return false;
  }
  QVector<OutgoingMessage> outgoing;
  QVector<StoredMessage> records;
  for (const auto &input : inputs) {
    const bool image = input.msgFlag == "image";
    if (image && data->IsGroup()) {
      emit status(data->GetThreadId(), tr("群聊暂支持文字和表情，请移除图片后发送"));
      return false;
    }
    if (image && (input.imageData.isEmpty() || input.imageData.size() > 20 * 1024 * 1024)) {
      emit status(data->GetThreadId(), tr("图片无法读取或超过 20 MB，请重新选择图片"));
      return false;
    }
    if (!image && input.content.trimmed().isEmpty())
      continue;
    const auto parts = image ? QStringList{QString()} : SplitText(input.content, data);
    for (const auto &part : parts) {
      const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
      const auto message = std::make_shared<TextChatData>(
          id, data->GetThreadId(), ChatFormType::PRIVATE,
          image ? ChatMsgType::PIC : ChatMsgType::TEXT, image ? "pending:" + id : part,
          UserMgr::GetInstance()->GetUid(), UN_READ,
          QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"));
      auto stored = StoredMessage::From(message);
      stored.localImage = input.imageData;
      records.append(stored);
      MsgInfo content = input;
      content.content = part;
      outgoing.append({content, data, message});
    }
  }
  if (records.isEmpty())
    return false;
  _saving = true;
  emit savingChanged(true);
  const auto revision = _revision;
  const QPointer<MessageSender> guard(this);
  ChatStore::instance()->SaveMessages(
      records, 0, -1,
      [guard, revision, data, outgoing](const ChatStoreResult &result) {
        if (!guard || guard->_revision != revision)
          return;
        guard->_saving = false;
        emit guard->savingChanged(false);
        if (!guard->IsCurrent(data))
          return;
        if (!result.error.isEmpty()) {
          emit guard->status(data->GetThreadId(), guard->tr("发送前保存失败，输入内容已保留"));
          return;
        }
        emit guard->outgoingSaved(data->GetThreadId());
        for (const auto &item : outgoing) {
          const auto id = item.message->GetUniqueId();
          data->AppendUnRspMsg(id, item.message);
          if (!item.content.imageData.isEmpty())
            ResourceMgr::instance()->rememberImage(item.message->GetContent(),
                                                   item.content.imageData);
          guard->_sendQueue.enqueue(item);
        }
        emit guard->messagesChanged(data->GetThreadId(), true);
        emit guard->status(data->GetThreadId(), {});
        guard->SendNextMessage();
      },
      data->GetThreadId());
  return true;
}

void MessageSender::MarkOutgoingFailed(const std::shared_ptr<ChatDataBase> &message,
                                       const std::shared_ptr<ChatThreadData> &data, bool definitive)
{
  if (!message || message->GetMsgId() > 0)
    return;
  message->SetStatus(SEND_FAILED);
  ChatStore::instance()->MarkUnconfirmed(message->GetUniqueId(), definitive);
  emit messageStatusChanged(data->GetThreadId(), message->GetUniqueId(), SEND_FAILED);
}

void MessageSender::RetryMessage(const std::shared_ptr<TextChatData> &message,
                                 const std::shared_ptr<ChatThreadData> &data)
{
  if (!IsCurrent(data) || !message || message->GetMsgId() > 0 ||
      _imageUploads.contains(message->GetUniqueId()) ||
      _retryingIds.contains(message->GetUniqueId()) || !ChatStore::instance()->IsReady() ||
      !TcpMgr::GetInstance()->SupportsMessageDedup() || !TcpMgr::GetInstance()->IsConnected() ||
      UserMgr::GetInstance()->GetChatThreadByThreadId(data->GetThreadId()) != data)
    return;
  const auto id = message->GetUniqueId();
  _retryingIds.insert(id);
  const QPointer<MessageSender> guard(this);
  const auto revision = _revision;
  const auto retry = [guard, revision, message, data, id](const ChatStoreResult &image) {
    if (!guard || guard->_revision != revision)
      return;
    if (!guard->IsCurrent(data)) {
      guard->_retryingIds.remove(id);
      return;
    }
    const bool picture = message->GetMsgType() == ChatMsgType::PIC;
    const bool needsUpload = picture && !image.image.isEmpty();
    if (!image.error.isEmpty() ||
        (picture && message->GetContent().startsWith("pending:") && image.image.isEmpty())) {
      guard->_retryingIds.remove(id);
      emit guard->status(data->GetThreadId(), guard->tr("本机图片无法读取，暂时不能重试"));
      return;
    }
    if (message->GetMsgId() > 0) {
      guard->_retryingIds.remove(id);
      return;
    }
    message->SetStatus(UN_READ);
    auto record = StoredMessage::From(message);
    record.localImage = image.image;
    ChatStore::instance()->SaveMessages(
        {record}, 0, -1,
        [guard, revision, message, data, id, image, needsUpload](const ChatStoreResult &result) {
          if (!guard || guard->_revision != revision)
            return;
          guard->_retryingIds.remove(id);
          if (!guard->IsCurrent(data))
            return;
          if (!result.error.isEmpty()) {
            guard->MarkOutgoingFailed(message, data);
            return;
          }
          MsgInfo content;
          content.msgFlag = needsUpload ? "image" : "text";
          content.content = message->GetContent();
          content.imageData = image.image;
          guard->_sendQueue.enqueue({content, data, message});
          emit guard->messageStatusChanged(data->GetThreadId(), id, -1);
          guard->SendNextMessage();
        });
  };
  if (message->GetMsgType() == ChatMsgType::PIC)
    ChatStore::instance()->LoadPendingImage(id, retry);
  else
    retry(ChatStoreResult{});
}

void MessageSender::SendNextMessage()
{
  if (!_imageUploads.isEmpty() || _uploadCommitting)
    return;
  while (!_sendQueue.isEmpty()) {
    const auto item = _sendQueue.dequeue();
    const auto data = item.chat;
    if (UserMgr::GetInstance()->GetChatThreadByThreadId(data->GetThreadId()) != data)
      continue;
    if (!TcpMgr::GetInstance()->IsConnected() ||
        (!data->IsGroup() && !UserMgr::GetInstance()->CheckFriendById(data->GetOtherId()))) {
      MarkOutgoingFailed(item.message, data);
      emit status(data->GetThreadId(), tr("会话无法发送，消息保留在本机待确认记录中"));
      continue;
    }
    if (item.content.msgFlag == "image") {
      const auto id = item.message->GetUniqueId();
      _imageUploads.insert(id, item);
      ResourceMgr::instance()->uploadImage(item.content.imageData, data->GetOtherId(), id);
      return;
    }
    SendStoredMessage(item.message, data);
  }
}

QStringList MessageSender::SplitText(const QString &text,
                                     const std::shared_ptr<ChatThreadData> &data)
{
  // 沿用客户端 2KB 内的保守预算；完整表情标记和代理对不能拆开。
  QStringList parts;
  QString part;
  const QString sampleId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const int sender = UserMgr::GetInstance()->GetUid();
  const int receiver = data->GetOtherId();
  const int thread = data->GetThreadId();
  for (int i = 0; i < text.size(); ++i) {
    QString symbol(text[i]);
    if (text[i] == QLatin1Char('[')) {
      const auto end = text.indexOf(QLatin1Char(']'), i + 1);
      if (end > i && end - i < EmojiCatalog::MaximumTokenLength) {
        const auto token = text.mid(i, end - i + 1);
        if (EmojiCatalog::FindByToken(token)) {
          symbol = token;
          i = int(end);
        }
      }
    } else if (text[i].isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate())
      symbol += text[++i];
    const QString candidate = part + symbol;
    if (!part.isEmpty() &&
        (escapedJsonSize(candidate) > 600 ||
         textPacket(candidate, sampleId, sender, receiver, thread).size() > 1800)) {
      parts.append(part);
      part.clear();
    }
    part += symbol;
  }
  if (!part.isEmpty())
    parts.append(part);
  return parts;
}

void MessageSender::SendStoredMessage(const std::shared_ptr<TextChatData> &message,
                                      const std::shared_ptr<ChatThreadData> &data)
{
  if (message->GetMsgId() > 0)
    return;
  if (!TcpMgr::GetInstance()->IsConnected() ||
      UserMgr::GetInstance()->GetChatThreadByThreadId(data->GetThreadId()) != data ||
      (!data->IsGroup() && !UserMgr::GetInstance()->CheckFriendById(data->GetOtherId()))) {
    MarkOutgoingFailed(message, data);
    return;
  }
  const auto id = message->GetUniqueId();
  emit TcpMgr::GetInstance() -> sig_send_data(
      ID_TEXT_CHAT_MSG_REQ,
      textPacket(message->GetContent(), id, message->GetSendUid(), data->GetOtherId(),
                 data->GetThreadId(), message->GetMsgType() == ChatMsgType::PIC ? 1 : 0));
  const auto generation = ChatStore::instance()->Generation();
  const auto revision = _revision;
  const auto attempt = ++_nextAttempt;
  _attempts.insert(id, attempt);
  QTimer::singleShot(15000, this, [this, data, message, id, generation, revision, attempt] {
    if (_revision != revision || _attempts.value(id) != attempt)
      return;
    _attempts.remove(id);
    if (!IsCurrent(data) || ChatStore::instance()->Generation() != generation ||
        !data->GetMsgUnRspRef().contains(id))
      return;
    MarkOutgoingFailed(message, data);
    emit status(data->GetThreadId(),
                tr("消息未收到确认，已保留在本机；下次同步会核对，暂不自动重发"));
  });
}
