#include "appbridge.h"
#include "mobilemedia.h"
#include "common/attachmentpreparer.h"
#include "common/fileattachment.h"
#include "network/resourcemgr.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/messagesender.h"
#include <QFutureWatcher>
#include <QtConcurrentRun>

void AppBridge::initializeMedia()
{
  m_media = new MobileMedia(this);
  connect(m_media, &MobileMedia::failed, this, &AppBridge::operationFailed);
  connect(m_media, &MobileMedia::status, this, &AppBridge::statusMessage);
  auto *resources = ResourceMgr::instance();
  connect(resources, &ResourceMgr::uploadProgress, this, [this](const QString &id, qint64 done, qint64 total) {
    emit transferProgress(id, done, total, true);
  });
  connect(resources, &ResourceMgr::downloadProgress, this, [this](const QString &id, qint64 done, qint64 total) {
    emit transferProgress(id, done, total, false);
  });
  connect(resources, &ResourceMgr::imageReady, this, [this](const QString &id, const QByteArray &bytes, const QString &error) {
    if (!m_requestedFiles.remove(id))
      return;
    if (!error.isEmpty())
      return; // connectChatBackends already presents the resource error.
    if (!FileAttachment::Unpack(bytes).valid)
      emit operationFailed(tr("文件内容或文件名无效，未打开或保存"));
    else
      emit fileReady(id);
  });
  connect(m_sender, &MessageSender::sendPrepared, this, [this](bool success) {
    if (m_savingVoice.isEmpty())
      return;
    if (success && m_media->draft() == m_savingVoice)
      m_media->clearDraft();
    m_savingVoice.clear();
  });
}
QVariantMap AppBridge::mediaCapabilities() const
{
  const auto tcp = TcpMgr::GetInstance();
  return {{"voice", tcp->SupportsVoiceMessages()}, {"files", tcp->SupportsFileMessages()},
          {"call", tcp->SupportsVoiceCall()}, {"video", tcp->SupportsVideoCall()}};
}
bool AppBridge::sendFile(int threadId, const QUrl &file)
{
  const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId);
  if (sending() || !storeReady() || !isLoggedIn() || !data || !file.isValid() ||
      (!file.isLocalFile() && file.scheme() != "content"))
    return false;
  if (!TcpMgr::GetInstance()->SupportsFileMessages()) {
    emit operationFailed(tr("当前服务器尚未支持文件传输"));
    return false;
  }
  m_imageThread = data;
  m_attachmentKind = "file";
  m_pendingImage = AttachmentPreparer::instance()->FromDocument(file.isLocalFile() ? file.toLocalFile() : file.toString());
  if (m_pendingImage->ready) {
    const auto error = m_pendingImage->error;
    m_pendingImage.reset();
    m_imageThread.reset();
    emit operationFailed(error);
    return false;
  }
  emit sendingChanged();
  return true;
}
void AppBridge::startRecording(int threadId)
{
  if (storeReady() && isLoggedIn() && !sending() && TcpMgr::GetInstance()->SupportsVoiceMessages())
    m_media->startRecording(threadId);
}
bool AppBridge::sendVoice(int threadId)
{
  const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId);
  if (!data || sending() || !storeReady() || !isLoggedIn() ||
      m_media->recordingThread() != threadId || m_media->draft().isEmpty() || m_media->busy())
    return false;
  MsgInfo input;
  input.msgFlag = "audio";
  input.imageData = m_media->draft();
  m_savingVoice = input.imageData;
  if (m_sender->Send({input}, data, false))
    return true;
  m_savingVoice.clear();
  return false;
}
void AppBridge::startCall(int threadId, bool video)
{
  const auto data = UserMgr::GetInstance()->GetChatThreadByThreadId(threadId);
  const auto tcp = TcpMgr::GetInstance();
  if (!data || data->IsGroup() || !storeReady() || !isLoggedIn() || !tcp->SupportsVoiceCall() ||
      (video && !tcp->SupportsVideoCall())) {
    emit operationFailed(tr("当前会话或服务器不支持此类通话"));
    return;
  }
  m_media->startCall(data->GetOtherId(), video);
}
QVariantMap AppBridge::fileInfo(const QString &id) const
{
  const auto file = FileAttachment::Unpack(ResourceMgr::instance()->cachedMedia(id));
  return {{"ready", file.valid}, {"name", file.name}, {"size", file.bytes.size()}};
}
void AppBridge::downloadFile(const QString &id)
{
  if (!storeReady() || id.isEmpty() || m_requestedFiles.contains(id))
    return;
  m_requestedFiles.insert(id);
  ResourceMgr::instance()->fetchMedia(id);
}
void AppBridge::saveFile(const QString &id, const QUrl &destination)
{
  if (!storeReady() || !destination.isValid() ||
      (!destination.isLocalFile() && destination.scheme() != "content"))
    return;
  const auto document = FileAttachment::Unpack(ResourceMgr::instance()->cachedMedia(id));
  if (!document.valid) {
    emit operationFailed(tr("文件尚未下载，请重新点击下载"));
    return;
  }
  auto *watcher = new QFutureWatcher<QString>(this);
  connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
    const auto error = watcher->result();
    watcher->deleteLater();
    if (error.isEmpty())
      emit statusMessage(tr("文件已保存到所选位置"));
    else
      emit operationFailed(error);
  });
  const auto path = destination.isLocalFile() ? destination.toLocalFile() : destination.toString();
  watcher->setFuture(QtConcurrent::run([document, path] {
    QString error;
    FileAttachment::Save(document, path, error);
    return error;
  }));
}
