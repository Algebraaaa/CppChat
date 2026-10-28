#include "chatpage.h"
#include "chathistorydialog.h"
#include "common/attachmentpreparer.h"
#include "common/chatstore.h"
#include "messageactions.h"
#include "messagemenu.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/messagesender.h"
#include "services/voicemessageaudio.h"
#include "ui_chatpage.h"
#include "voicecalldialog.h"
#include "voicemessagedialog.h"
#include "widgets/audiobubble.h"
#include "widgets/chatitembase.h"
#include "widgets/emojipicker.h"
#include "widgets/filebubble.h"
#include "widgets/picturebubble.h"
#include "widgets/screencapture.h"
#include "widgets/textbubble.h"
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHideEvent>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyleOption>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <utility>

namespace {
QString messageScrollKey(const std::shared_ptr<ChatDataBase> &message)
{
  return message->GetUniqueId().isEmpty() ? QStringLiteral("server:%1").arg(message->GetMsgId())
                                          : QStringLiteral("unique:") + message->GetUniqueId();
}

QDateTime parseChatTime(const QString &value)
{
  QDateTime time = QDateTime::fromString(value, "yyyy-MM-dd HH:mm:ss.zzz");
  if (!time.isValid())
    time = QDateTime::fromString(value, "yyyy-MM-dd HH:mm:ss");
  return time;
}

QString displayChatTime(const QDateTime &time)
{
  const QDate today = QDate::currentDate();
  if (time.date() == today)
    return time.toString("HH:mm");
  if (time.date() == today.addDays(-1))
    return QObject::tr("昨天 %1").arg(time.toString("HH:mm"));
  if (time.date().year() == today.year())
    return time.toString("MM-dd HH:mm");
  return time.toString("yyyy-MM-dd HH:mm");
}
} // namespace

ChatPage::ChatPage(QWidget *parent) : QWidget(parent), ui(new Ui::ChatPage)
{
  ui->setupUi(this);
  _sender = new MessageSender(this);
  connect(_sender, &MessageSender::sendPrepared, this, [this](bool success) {
    ShowStatus(success ? tr("已保存到本机待发送队列") : tr("消息保存失败，原内容未发送，请重试"));
  });
  _messageActions = new MessageActions(this);
  connect(_messageActions, &MessageActions::reminderRequested, this,
          &ChatPage::sig_reminder_requested);
  SetupSelection();
  connect(_messageActions, &MessageActions::selectionRequested, this, &ChatPage::BeginSelection);
  const auto removed = [this](int thread, const QVector<int> &ids) {
    if (!_chat_data || _chat_data->GetThreadId() != thread)
      return;
    for (int id : ids)
      _selected.remove(id);
    UpdateSelection();
  };
  connect(ChatStore::instance(), &ChatStore::localMessagesDeleted, this, removed);
  connect(ChatStore::instance(), &ChatStore::localMessageDeleted, this,
          [removed](int thread, int id) { removed(thread, {id}); });
  connect(ChatStore::instance(), &ChatStore::accountChanged, this, &ChatPage::EndSelection);
  connect(_messageActions, &MessageActions::forwardRequested, this,
          &ChatPage::sig_forward_messages);
  connect(_messageActions, &MessageActions::searchRequested, this, [this](int thread) {
    if (_chat_data && _chat_data->GetThreadId() == thread)
      OpenHistory();
  });
  connect(_messageActions, &MessageActions::quoteRequested, this,
          [this](int thread, const QString &sender, const QString &text) {
            if (_chat_data && _chat_data->GetThreadId() == thread && ui->chatEdit->isEnabled())
              ui->chatEdit->insertQuote(sender, text);
          });
  connect(_messageActions, &MessageActions::status, this, [this](int thread, const QString &text) {
    if (_chat_data && _chat_data->GetThreadId() == thread)
      ShowStatus(text);
  });
  connect(ui->chat_settings_panel, &ChatSettingsPanel::sig_members_requested, this, [this] {
    emit sig_group_settings(_chat_data && _chat_data->IsGroup() ? _chat_data->GetThreadId() : 0);
  });
  setAttribute(Qt::WA_StyledBackground);
  ui->chat_data_wid->setAttribute(Qt::WA_StyledBackground);
  ui->title_wid->setAttribute(Qt::WA_StyledBackground);
  ui->title_label->installEventFilter(this);
  ui->composer_wid->setAttribute(Qt::WA_StyledBackground);
  ui->composer_card->setAttribute(Qt::WA_StyledBackground);
  ui->tool_wid->setAttribute(Qt::WA_StyledBackground);
  ui->chat_splitter->setStretchFactor(0, 1);
  ui->chat_splitter->setStretchFactor(1, 0);
  ui->chat_splitter->setSizes({430, 208});
  ui->emo_btn->setToolTip(tr("表情"));
  ui->file_btn->setToolTip(tr("发送图片或文件"));
  ui->file_btn->setAccessibleName(tr("发送附件"));
  ui->favorites_btn->setCursor(Qt::PointingHandCursor);
  ui->favorites_btn->setAccessibleName(tr("收藏"));
  connect(ui->favorites_btn, &QPushButton::clicked, this, &ChatPage::sig_open_favorites);
  _voice = new VoiceCallDialog(this);
  connect(_voice, &VoiceCallDialog::status, this, &ChatPage::ShowStatus);
  SetupMediaControls();
  ui->screenshot_btn->setCursor(Qt::PointingHandCursor);
  ui->chat_more_btn->setCursor(Qt::PointingHandCursor);
  ui->chat_more_btn->setAccessibleName(tr("聊天设置"));
  ui->chat_history_btn->setAccessibleName(tr("查找聊天记录"));
  connect(ui->chat_history_btn, &QPushButton::clicked, this, &ChatPage::OpenHistory);
  connect(ui->chat_settings_panel, &ChatSettingsPanel::sig_history_requested, this,
          &ChatPage::OpenHistory);
  ui->chat_settings_panel->SetToggleButton(ui->chat_more_btn);
  connect(ui->chat_more_btn, &QPushButton::clicked, this, [this] {
    if (_chat_data) {
      UpdateSettingsRect();
      ui->chat_settings_panel->Toggle();
    }
  });
  connect(ui->chat_settings_panel, &ChatSettingsPanel::sig_pin_changed, this, [this](bool pinned) {
    if (_chat_data)
      emit sig_thread_pin_changed(_chat_data->GetThreadId(), pinned);
  });
  connect(ui->chat_settings_panel, &ChatSettingsPanel::sig_mute_changed, this, [this](bool muted) {
    if (_chat_data)
      emit sig_thread_mute_changed(_chat_data->GetThreadId(), muted);
  });
  ui->screenshot_btn->setAccessibleName(tr("截屏"));
  connect(ui->screenshot_btn, &QPushButton::clicked, this, &ChatPage::StartScreenCapture);
  _draftTimer = new QTimer(this);
  _draftTimer->setSingleShot(true);
  _draftTimer->setInterval(500);
  connect(_draftTimer, &QTimer::timeout, this, &ChatPage::SaveCurrentDraft);
  connect(ui->chat_settings_panel, &ChatSettingsPanel::sig_clear_history, this, [this] {
    const auto data = _chat_data;
    if (!data || !ChatStore::instance()->IsReady())
      return;
    const auto generation = ChatStore::instance()->Generation();
    if (QMessageBox::question(this, tr("清空本机聊天记录"),
                              tr("清空本机已确认的聊天记录？服务器记录、图片缓存、待确认消息、本机"
                                 "收藏、笔记和提醒会保留。"),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
      return;
    if (generation != ChatStore::instance()->Generation() || _chat_data != data)
      return;
    EndSelection();
    const QPointer<ChatPage> guard(this);
    ChatStore::instance()->ClearLocalHistory(data->GetThreadId(),
                                             [guard, data](const ChatStoreResult &result) {
                                               if (guard && result.error.isEmpty()) {
                                                 data->ClearLocalMessages();
                                                 if (guard->_historyDialog)
                                                   guard->_historyDialog->Reset();
                                                 emit guard->sig_reload_local(data->GetThreadId());
                                               }
                                             });
  });
  // 没有选中会话或输入内容只有空白字符时，发送按钮不可用。
  connect(ui->chatEdit, &QTextEdit::textChanged, this, [this] {
    UpdateSendButton();
    if (_chat_data && !_restoringDraft && !_sender->SavingMessages() &&
        !_loadingDrafts.contains(_chat_data->GetThreadId())) {
      _dirtyDrafts.insert(_chat_data->GetThreadId());
      _draftTimer->start();
    }
  });
  connect(ui->chatEdit, &MessageTextEdit::attachmentsChanged, this, [this] {
    UpdateSendButton();
    if (_chat_data && !_restoringDraft && !_sender->SavingMessages()) {
      _dirtyDrafts.insert(_chat_data->GetThreadId());
      SaveCurrentDraft();
    }
  });
  connect(AttachmentPreparer::instance(), &AttachmentPreparer::prepared, this,
          [this](const QString &id) {
            const auto threads = _drafts.keys();
            for (int threadId : threads) {
              if (_chat_data && _chat_data->GetThreadId() == threadId)
                continue;
              const auto draft = _drafts.value(threadId);
              for (const auto &part : draft) {
                if (part.preparation && part.preparation->id == id) {
                  _dirtyDrafts.insert(threadId);
                  PersistDraft(threadId);
                  break;
                }
              }
            }
          });
  connect(ui->send_btn, &QPushButton::clicked, this, &ChatPage::on_send_btn_clicked);
  connect(ui->chatEdit, &MessageTextEdit::send, this, &ChatPage::on_send_btn_clicked);
  connect(ui->chatEdit, &MessageTextEdit::attachmentError, this, &ChatPage::ShowStatus);
  auto *attachmentMenu = new QMenu(ui->file_btn);
  attachmentMenu->setObjectName("attachmentMenu");
  _sendImageAction = attachmentMenu->addAction(tr("图片…"));
  _sendImageAction->setObjectName("sendImageAction");
  _sendFileAction = attachmentMenu->addAction(tr("文件…"));
  _sendFileAction->setObjectName("sendFileAction");
  ui->file_btn->setMenu(attachmentMenu);
  connect(_sendFileAction, &QAction::triggered, this, &ChatPage::SendDocument);
  connect(_sendImageAction, &QAction::triggered, this, [this] {
    if (!ui->chatEdit->isEnabled())
      return;
    if (!_chat_data || !TcpMgr::GetInstance()->IsConnected()) {
      ShowStatus(tr("请先选择会话并连接聊天服务器"));
      return;
    }
    if (_chat_data->IsGroup() && !TcpMgr::GetInstance()->SupportsGroupImages()) {
      ShowStatus(tr("服务器尚不支持群图片，请升级服务器后重试"));
      return;
    }
    if (!_chat_data->IsGroup() &&
        !UserMgr::GetInstance()->CheckFriendById(_chat_data->GetOtherId())) {
      ShowStatus(errorCodeMessage(FriendNotFound));
      return;
    }
    const auto files = QFileDialog::getOpenFileNames(
        this, tr("发送图片"), {}, tr("图片 (*.png *.jpg *.jpeg *.gif *.webp *.bmp)"));
    ui->chatEdit->insertFileFromUrl(files);
    ui->chatEdit->setFocus();
  });
  connect(_sender, &MessageSender::savingChanged, this,
          [this, focusThread = 0, fallback = QPointer<QWidget>()](bool saving) mutable {
            if (saving)
              focusThread = ui->chatEdit->hasFocus() && _chat_data ? _chat_data->GetThreadId() : 0;
            ui->chatEdit->setEnabled(!saving && _chat_data &&
                                     !_loadingDrafts.contains(_chat_data->GetThreadId()));
            UpdateSendButton();
            if (saving) {
              // Disabling the editor moves focus. Restore it only if the user has
              // not since selected another control, conversation or window.
              fallback = QApplication::focusWidget();
            } else {
              const bool restore =
                  focusThread > 0 && _chat_data && _chat_data->GetThreadId() == focusThread &&
                  ui->chatEdit->isEnabled() && ui->chatEdit->isVisible() &&
                  window()->isActiveWindow() && QApplication::focusWidget() == fallback;
              focusThread = 0;
              fallback.clear();
              if (restore)
                ui->chatEdit->setFocus(Qt::OtherFocusReason);
            }
          });
  connect(_sender, &MessageSender::outgoingSaved, this, [this](int threadId) {
    _drafts.remove(threadId);
    _dirtyDrafts.remove(threadId);
    if (_chat_data && _chat_data->GetThreadId() == threadId) {
      _restoringDraft = true;
      ui->chatEdit->clear();
      _restoringDraft = false;
    }
  });
  connect(_sender, &MessageSender::messagesChanged, this, [this](int threadId, bool bottom) {
    if (_chat_data && _chat_data->GetThreadId() == threadId) {
      SetChatData(_chat_data);
      if (bottom)
        ui->chat_data_list->scrollToBottom();
    }
    emit sig_messages_changed(threadId);
  });
  connect(_sender, &MessageSender::messageStatusChanged, this,
          [this](int threadId, const QString &id, int status) {
            if (_chat_data && _chat_data->GetThreadId() == threadId)
              UpdateChatStatus(id, status);
          });
  connect(_sender, &MessageSender::status, this, [this](int threadId, const QString &message) {
    if (_chat_data && _chat_data->GetThreadId() == threadId)
      ShowStatus(message);
  });
  auto *emojiPicker = new EmojiPicker(this);
  auto *emojiButton = ui->emo_btn;
  const QPointer<MessageTextEdit> messageEdit(ui->chatEdit);
  emojiButton->setCheckable(true);
  connect(emojiPicker, &EmojiPicker::visibilityChanged, emojiButton,
          [emojiButton, messageEdit](bool visible) {
            emojiButton->setChecked(visible);
            if (!visible && messageEdit && messageEdit->isVisible() && messageEdit->isEnabled())
              messageEdit->setFocus(Qt::PopupFocusReason);
          });
  connect(emojiPicker, &EmojiPicker::emojiSelected, this, [this](const QString &id) {
    if (!ui->chatEdit->isEnabled())
      return;
    ui->chatEdit->setFocus();
    ui->chatEdit->insertEmoji(id);
  });
  connect(ui->emo_btn, &QPushButton::clicked, this, [this, emojiPicker] {
    if (ui->chatEdit->isEnabled())
      emojiPicker->PopupAt(ui->emo_btn, UserMgr::GetInstance()->GetUid());
  });
  Reset();
}
ChatPage::~ChatPage()
{
  if (_voiceMessage)
    delete _voiceMessage.data();
  // Finish while ui and status callbacks still exist; QObject deletes children later.
  _voice->finish();
  delete _voice;
  _voice = nullptr;
  SaveCurrentDraft();
  // 截屏控制器的回调依赖 ui，先释放控制器，再释放页面控件。
  delete _screenCapture.data();
  delete ui;
}

void ChatPage::SetupMediaControls()
{
  auto *menu = new QMenu(ui->call_btn);
  menu->setObjectName("chat_call_menu");
  auto *audio = menu->addAction(QIcon(":/icons/call-phone.svg"), tr("语音通话"));
  auto *video = menu->addAction(QIcon(":/icons/call-video.svg"), tr("视频通话"));
  connect(audio, &QAction::triggered, this, [this] { StartCall(false); });
  connect(video, &QAction::triggered, this, [this] { StartCall(true); });
  connect(menu, &QMenu::aboutToShow, this, [this] { ui->chat_settings_panel->Close(false); });
  ui->call_btn->setMenu(menu);

  // 输入工具与实时通话是独立入口，尚未接入的能力不打开通话窗口或设备。
  for (auto *button : {ui->voice_transcribe_btn}) {
    connect(button, &QPushButton::clicked, this,
            [this, button] { ShowStatus(tr("%1功能暂未开放").arg(button->accessibleName())); });
  }
  connect(ui->voice_message_btn, &QPushButton::clicked, this, &ChatPage::RecordVoiceMessage);
}

void ChatPage::RecordVoiceMessage()
{
  const auto data = _chat_data;
  if (_voiceMessage) {
    _voiceMessage->raise();
    return;
  }
  if (!data || !ChatStore::instance()->IsReady() || !TcpMgr::GetInstance()->IsConnected() ||
      !TcpMgr::GetInstance()->SupportsVoiceMessages() ||
      VoiceMessageAudio::instance()->CallBusy()) {
    ShowStatus(tr("请连接支持语音消息的服务器，并结束当前通话后重试"));
    return;
  }
  auto *dialog = new VoiceMessageDialog(this);
  _voiceMessage = dialog;
  const QPointer<VoiceMessageDialog> guard(dialog);
  connect(dialog, &VoiceMessageDialog::sendRequested, this,
          [this, data, guard](const QByteArray &wave) {
            if (!guard || _chat_data != data)
              return;
            MsgInfo input;
            input.msgFlag = "audio";
            input.imageData = wave;
            if (!_sender->Send({input}, data, false))
              guard->SetSending(false);
          });
  connect(_sender, &MessageSender::sendPrepared, dialog, [guard](bool success) {
    if (!guard || !guard->Sending())
      return;
    if (success)
      guard->accept();
    else
      guard->SetSending(false);
  });
  dialog->show();
}

void ChatPage::StartCall(bool video)
{
  if (!_chat_data || _chat_data->IsGroup()) {
    ShowStatus(tr("请选择好友进行一对一通话"));
    return;
  }
  CallContact(_chat_data->GetOtherId(), video);
}

void ChatPage::CallContact(int uid, bool video)
{
  if (uid == UserMgr::GetInstance()->GetUid() || !UserMgr::GetInstance()->CheckFriendById(uid)) {
    ShowStatus(tr("请选择好友进行一对一通话"));
    return;
  }
  _voice->call(uid, video);
}

void ChatPage::UpdateMediaControls()
{
  _sendFileAction->setEnabled(bool(_chat_data) && TcpMgr::GetInstance()->SupportsFileMessages());
  _sendImageAction->setEnabled(
      _chat_data && (!_chat_data->IsGroup() || TcpMgr::GetInstance()->SupportsGroupImages()));
  ui->file_btn->setEnabled(_sendFileAction->isEnabled() || _sendImageAction->isEnabled());
  const bool canCall = _chat_data && !_chat_data->IsGroup();
  if (!canCall)
    ui->call_btn->menu()->close();
  ui->call_btn->setEnabled(canCall);
  ui->call_btn->setToolTip(_chat_data && _chat_data->IsGroup() ? tr("暂不支持群通话")
                                                               : tr("语音/视频通话"));
  ui->voice_transcribe_btn->setEnabled(bool(_chat_data));
  ui->voice_message_btn->setEnabled(bool(_chat_data) &&
                                    TcpMgr::GetInstance()->SupportsVoiceMessages());
  ui->voice_message_btn->setToolTip(TcpMgr::GetInstance()->SupportsVoiceMessages()
                                        ? tr("录制并发送语音消息")
                                        : tr("请升级服务器以发送语音消息"));
}

void ChatPage::StartScreenCapture()
{
  if (_screenCapture || !ui->chatEdit->isEnabled())
    return;
  if (!_chat_data || !TcpMgr::GetInstance()->IsConnected()) {
    ShowStatus(tr("请先选择会话并连接聊天服务器"));
    return;
  }
  if (_chat_data->IsGroup() && !TcpMgr::GetInstance()->SupportsGroupImages()) {
    ShowStatus(tr("服务器尚不支持群图片，请升级服务器后重试"));
    return;
  }
  if (!_chat_data->IsGroup() &&
      !UserMgr::GetInstance()->CheckFriendById(_chat_data->GetOtherId())) {
    ShowStatus(errorCodeMessage(FriendNotFound));
    return;
  }
  const auto conversation = _chat_data;
  auto *capture = new ScreenCapture(window(), this);
  _screenCapture = capture;
  ui->screenshot_btn->setEnabled(false);
  ShowStatus({});
  connect(capture, &ScreenCapture::captured, this, [this, conversation](const QImage &image) {
    // 截屏期间网络和会话仍能变化，不向另一段会话插入图片。
    if (_sender->SavingMessages() || _chat_data != conversation ||
        !TcpMgr::GetInstance()->IsConnected() ||
        (conversation->IsGroup()
             ? !TcpMgr::GetInstance()->SupportsGroupImages()
             : !UserMgr::GetInstance()->CheckFriendById(conversation->GetOtherId()))) {
      ShowStatus(tr("会话已变化，请重新选择会话后截屏"));
      return;
    }
    ui->chatEdit->insertImage(image);
    ui->chatEdit->setFocus();
  });
  connect(capture, &ScreenCapture::failed, this, &ChatPage::ShowStatus);
  connect(capture, &ScreenCapture::finished, this, [this, capture] {
    _screenCapture = nullptr;
    ui->screenshot_btn->setEnabled(bool(_chat_data));
    capture->deleteLater();
  });
  capture->start();
}

void ChatPage::SendDocument()
{
  const auto conversation = _chat_data;
  if (!conversation || _sender->SavingMessages() ||
      !TcpMgr::GetInstance()->SupportsFileMessages()) {
    ShowStatus(tr("当前会话或服务器尚不支持文件传输"));
    return;
  }
  auto *picker = new QFileDialog(this, tr("发送文件（最多 20 MiB）"));
  picker->setAttribute(Qt::WA_DeleteOnClose);
  picker->setFileMode(QFileDialog::ExistingFile);
  connect(picker, &QFileDialog::fileSelected, this, [this, conversation](const QString &path) {
    if (_chat_data != conversation || _sender->SavingMessages())
      return;
    const auto preparation = AttachmentPreparer::instance()->FromDocument(path);
    if (preparation->ready) {
      ShowStatus(preparation->error);
      return;
    }
    ShowStatus(tr("正在读取文件…"));
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(AttachmentPreparer::instance(), &AttachmentPreparer::prepared, this,
                          [this, preparation, conversation, connection](const QString &id) {
                            if (id != preparation->id)
                              return;
                            disconnect(*connection);
                            if (!preparation->error.isEmpty()) {
                              ShowStatus(preparation->error);
                              return;
                            }
                            if (_chat_data != conversation || !ChatStore::instance()->IsReady())
                              return;
                            MsgInfo file;
                            file.msgFlag = "file";
                            file.imageData = preparation->bytes;
                            _sender->Send({file}, conversation, false);
                          });
  });
  picker->open();
}

void ChatPage::OpenHistory()
{
  if (!_chat_data)
    return;
  ui->chat_settings_panel->Close(false);
  if (!_historyDialog)
    _historyDialog = new ChatHistoryDialog(this);
  _historyDialog->SetConversation(_chat_data->GetThreadId(), ui->title_label->text());
  if (_historyDialog->isMinimized())
    _historyDialog->showNormal();
  else
    _historyDialog->show();
  _historyDialog->raise();
  _historyDialog->activateWindow();
}

bool ChatPage::ForwardSnapshots(const QVector<StoredFavorite> &snapshots,
                                const std::shared_ptr<ChatThreadData> &target)
{
  if (snapshots.isEmpty() || snapshots.size() > 50)
    return false;
  QVector<MsgInfo> inputs;
  for (const auto &value : snapshots) {
    if (value.type != 0 && value.type != 1 && value.type != 3)
      return false;
    MsgInfo input;
    input.msgFlag = value.type == 3 ? "audio" : value.type == 1 ? "image" : "text";
    input.content = value.type == 0 ? value.content : QString();
    input.imageData = value.media;
    inputs.append(input);
  }
  // Forwarding is a new send, not an editor submission: preserve the destination draft.
  return _sender->Send(inputs, target, false);
}

QString ChatPage::ConversationTitle() const { return ui->title_label->text() + tr(" · CppChat"); }

void ChatPage::Reset()
{
  if (_voiceMessage)
    _voiceMessage->reject();
  VoiceMessageAudio::instance()->Cancel();
  EndSelection();
  if (_voice)
    _voice->finish();
  AttachmentPreparer::instance()->FinishPending();
  SaveCurrentDraft();
  for (int threadId : _dirtyDrafts.values())
    PersistDraft(threadId);
  _sender->Reset();
  _loadingDrafts.clear();
  if (_historyDialog)
    _historyDialog->Reset();
  ui->chat_history_btn->setEnabled(false);
  ui->chat_settings_panel->Close(false);
  ui->chat_settings_panel->SetPinned(false);
  ui->chat_more_btn->setEnabled(false);
  if (_screenCapture)
    _screenCapture->cancel();
  _messageActions->SetConversation(nullptr);
  _chat_data.reset();
  UpdateMediaControls();
  _pendingItems.clear();
  _renderedItems.clear();
  _renderedMessages.clear();
  _auxiliaryRows.clear();
  _drafts.clear();
  _dirtyDrafts.clear();
  ui->chat_data_list->removeAllItem();
  ui->chatEdit->clear();
  ui->chatEdit->setEnabled(false);
  ui->send_btn->setEnabled(false);
  ui->screenshot_btn->setEnabled(false);
  ui->title_label->setText(tr("选择好友开始聊天"));
  emit conversationTitleChanged(tr("CppChat"));
  ShowStatus({});
}

void ChatPage::SetChatData(std::shared_ptr<ChatThreadData> data)
{
  if (!data) {
    Reset();
    return;
  }
  const bool changed = !_chat_data || _chat_data->GetThreadId() != data->GetThreadId();
  if (changed) {
    if (_voiceMessage)
      _voiceMessage->reject();
    VoiceMessageAudio::instance()->Cancel();
  }
  const bool images = !data->IsGroup() || TcpMgr::GetInstance()->SupportsGroupImages();
  _sendFileAction->setEnabled(TcpMgr::GetInstance()->SupportsFileMessages());
  _sendImageAction->setEnabled(images);
  ui->file_btn->setEnabled(images || _sendFileAction->isEnabled());
  ui->file_btn->setToolTip(tr("发送图片或文件"));
  if (changed) {
    EndSelection();
    ui->call_btn->menu()->close();
    if (_historyDialog)
      _historyDialog->reject();
    ui->chat_settings_panel->Close(false);
    ui->chat_settings_panel->SetPinned(false);
  }
  if (changed && _screenCapture)
    _screenCapture->cancel();
  if (changed)
    SaveCurrentDraft();
  _chat_data = data;
  _messageActions->SetConversation(data);
  UpdateMediaControls();
  if (changed) {
    _restoringDraft = true;
    ui->chatEdit->setMessages(_drafts.value(data->GetThreadId()));
    _restoringDraft = false;
    if (!_drafts.contains(data->GetThreadId()) && !_loadingDrafts.contains(data->GetThreadId())) {
      _loadingDrafts.insert(data->GetThreadId());
      const QPointer<ChatPage> guard(this);
      ChatStore::instance()->LoadDraft(
          data->GetThreadId(), [guard, data](const ChatStoreResult &result) {
            if (!guard)
              return;
            guard->_loadingDrafts.remove(data->GetThreadId());
            if (result.error.isEmpty() && !guard->_drafts.contains(data->GetThreadId()))
              guard->_drafts.insert(data->GetThreadId(), result.draft);
            if (guard->_chat_data == data) {
              guard->_restoringDraft = true;
              guard->ui->chatEdit->setMessages(guard->_drafts.value(data->GetThreadId()));
              guard->_restoringDraft = false;
              guard->ui->chatEdit->setEnabled(!guard->_sender->SavingMessages());
              guard->UpdateSendButton();
            }
          });
    }
  }
  const auto user = UserMgr::GetInstance()->GetFriendById(data->GetOtherId());
  ui->chat_settings_panel->SetUser(user);
  if (data->IsGroup())
    ui->chat_settings_panel->SetGroup(data);
  ui->chat_more_btn->setEnabled(true);
  ui->chat_history_btn->setEnabled(true);
  ui->title_label->setTextFormat(Qt::PlainText);
  ui->title_label->setCursor(data->IsGroup() ? Qt::PointingHandCursor : Qt::ArrowCursor);
  ui->title_label->setFocusPolicy(data->IsGroup() ? Qt::StrongFocus : Qt::NoFocus);
  ui->title_label->setToolTip(data->IsGroup() ? tr("查看群成员和群聊详情") : QString());
  ui->title_label->setText(
      data->IsGroup()
          ? tr("%1（%2）").arg(data->GetGroupName()).arg(data->Group()["members"].toArray().size())
      : user ? (user->_back.isEmpty() ? user->_name : user->_back)
             : tr("用户 %1").arg(data->GetOtherId()));
  emit conversationTitleChanged(ui->title_label->text() + tr(" · CppChat"));
  ui->chatEdit->setEnabled(!_sender->SavingMessages() &&
                           !_loadingDrafts.contains(data->GetThreadId()));
  ui->screenshot_btn->setEnabled(!_screenCapture);
  UpdateSendButton();
  RenderMessages(changed);
}

void ChatPage::RenderMessages(bool changed)
{
  ui->chat_data_list->beginUpdate(changed);
  if (changed) {
    _renderedItems.clear();
    _renderedMessages.clear();
    _auxiliaryRows.clear();
    ui->chat_data_list->removeAllItem();
  }
  QHash<QString, ChatItemBase *> nextItems;
  QHash<QString, std::shared_ptr<ChatDataBase>> nextMessages;
  QHash<QString, QWidget *> nextAuxiliary;
  QList<QWidget *> rows;
  _pendingItems.clear();
  if (_chat_data->GetMsgMapRef().size() >= 50) {
    const auto key = QStringLiteral("older_records");
    auto *older = _auxiliaryRows.value(key);
    if (!older) {
      auto *button = new QPushButton(tr("查看更早的聊天记录"));
      button->setProperty("chat_scroll_key", key);
      connect(button, &QPushButton::clicked, this, &ChatPage::OpenHistory);
      older = button;
    }
    nextAuxiliary.insert(key, older);
    rows.append(older);
  }
  QDateTime previousTime;
  const auto append = [&](const std::shared_ptr<ChatDataBase> &message) {
    if (!message ||
        (message->GetMsgType() != ChatMsgType::TEXT && message->GetMsgType() != ChatMsgType::PIC &&
         message->GetMsgType() != ChatMsgType::AUDIO && message->GetMsgType() != ChatMsgType::FILE))
      return;
    const auto key = messageScrollKey(message);
    if (nextItems.contains(key))
      return;
    const auto time = parseChatTime(message->GetChatTime());
    if (time.isValid()) {
      if (!previousTime.isValid() || qAbs(previousTime.secsTo(time)) >= 300) {
        const auto timeKey = QStringLiteral("time:") + key;
        auto *label = qobject_cast<QLabel *>(_auxiliaryRows.value(timeKey));
        if (!label) {
          label = new QLabel;
          label->setObjectName("chat_time_separator");
          label->setProperty("chat_scroll_key", timeKey);
          label->setAlignment(Qt::AlignCenter);
          label->setMinimumHeight(30);
        }
        const auto text = displayChatTime(time);
        if (label->text() != text)
          label->setText(text);
        nextAuxiliary.insert(timeKey, label);
        rows.append(label);
      }
      previousTime = time;
    }
    auto *item = _renderedItems.value(key);
    // 保存正文快照，不能用旧 shared_ptr 比较：发送确认可能原地修改消息对象。
    if (!item || item->property("message_content").toString() != message->GetContent() ||
        item->property("message_type").toInt() != static_cast<int>(message->GetMsgType()) ||
        item->property("message_sender").toInt() != message->GetSendUid())
      item = CreateChatItem(message);
    item->setUserName(SenderName(message->GetSendUid()));
    item->setSelection(_selecting && message->GetMsgId() > 0,
                       _selected.contains(message->GetMsgId()));
    item->setStatus(message->GetMsgId() == 0 && message->GetStatus() != SEND_FAILED
                        ? -1
                        : message->GetStatus());
    if (message->GetMsgId() == 0)
      _pendingItems.insert(message->GetUniqueId(), item);
    nextItems.insert(key, item);
    nextMessages.insert(key, message);
    rows.append(item);
  };
  for (const auto &message : _chat_data->GetMsgMapRef())
    append(message);
  for (const auto &message : _chat_data->GetPendingMessages())
    append(message);
  _renderedItems = std::move(nextItems);
  _renderedMessages = std::move(nextMessages);
  _auxiliaryRows = std::move(nextAuxiliary);
  ui->chat_data_list->setChatItems(rows);
  ui->chat_data_list->endUpdate();
  if (_selecting) {
    for (auto it = _selected.begin(); it != _selected.end();) {
      const auto current = _chat_data->GetMsgMapRef().value(it.key());
      if (!current)
        it = _selected.erase(it);
      else {
        it.value() = current;
        ++it;
      }
    }
    UpdateSelection();
  }
}

void ChatPage::RemoveChatThread(int threadId, bool preserveLocalState)
{
  if (!preserveLocalState) {
    _drafts.remove(threadId);
    _dirtyDrafts.remove(threadId);
    _sender->RemoveThread(threadId);
  }
  if (!_chat_data || _chat_data->GetThreadId() != threadId)
    return;
  EndSelection();
  if (_historyDialog)
    _historyDialog->Reset();
  ui->chat_history_btn->setEnabled(false);
  ui->chat_settings_panel->Close(false);
  ui->chat_more_btn->setEnabled(false);
  if (_screenCapture)
    _screenCapture->cancel();
  _messageActions->SetConversation(nullptr);
  _chat_data.reset();
  UpdateMediaControls();
  _pendingItems.clear();
  _renderedItems.clear();
  _renderedMessages.clear();
  _auxiliaryRows.clear();
  ui->chat_data_list->removeAllItem();
  ui->chatEdit->clear();
  ui->chatEdit->setEnabled(false);
  ui->send_btn->setEnabled(false);
  ui->screenshot_btn->setEnabled(false);
  ui->title_label->setText(tr("选择好友开始聊天"));
}

bool ChatPage::eventFilter(QObject *watched, QEvent *event)
{
  if (_selecting && event->type() == QEvent::KeyPress &&
      static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
    const auto widget = qobject_cast<QWidget *>(watched);
    if (widget && widget->window() == window() && (widget == this || isAncestorOf(widget))) {
      EndSelection();
      return true;
    }
  }
  if (watched == ui->title_label && _chat_data && _chat_data->IsGroup()) {
    const bool keyboard = event->type() == QEvent::KeyPress &&
                          (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Return ||
                           static_cast<QKeyEvent *>(event)->key() == Qt::Key_Space);
    const bool mouse = event->type() == QEvent::MouseButtonRelease &&
                       static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton;
    if (mouse || keyboard) {
      emit sig_group_settings(_chat_data->GetThreadId());
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

QString ChatPage::SenderName(int uid) const
{
  if (!_chat_data || !_chat_data->IsGroup() || uid == UserMgr::GetInstance()->GetUid())
    return {};
  const auto user = UserMgr::GetInstance()->GetFriendById(uid);
  return user ? (user->_back.isEmpty() ? user->_name : user->_back) : _chat_data->MemberName(uid);
}

ChatItemBase *ChatPage::CreateChatItem(const std::shared_ptr<ChatDataBase> &message)
{
  const auto mgr = UserMgr::GetInstance();
  const bool self = message->GetSendUid() == mgr->GetUid();
  const auto user = self ? mgr->GetUserInfo() : mgr->GetFriendById(message->GetSendUid());
  const auto role = self ? ChatRole::Self : ChatRole::Other;
  auto *item = new ChatItemBase(role);
  const QString scrollKey = messageScrollKey(message);
  item->setProperty("chat_scroll_key", scrollKey);
  item->setProperty("message_content", message->GetContent());
  item->setProperty("message_type", static_cast<int>(message->GetMsgType()));
  item->setProperty("message_sender", message->GetSendUid());
  // Group messages identify the sender; private bubbles omit redundant names.
  item->setUserName(SenderName(message->GetSendUid()));
  const int senderUid = message->GetSendUid();
  const QString fallback = user ? user->_icon : QString();
  QPixmap icon = mgr->AvatarPixmap(senderUid, fallback);
  item->setUserIcon(icon);
  connect(mgr.get(), &UserMgr::sig_avatar_ready, item, [mgr, item, senderUid, fallback](int uid) {
    if (uid == senderUid)
      item->setUserIcon(mgr->AvatarPixmap(uid, fallback));
  });
  QPointer<PictureBubble> pictureBubble;
  if (message->GetMsgType() == ChatMsgType::PIC) {
    const auto fileId = message->GetContent();
    auto *bubble = new PictureBubble(role);
    pictureBubble = bubble;
    item->setWidget(bubble);
    bubble->loadImage(fileId);
  } else if (message->GetMsgType() == ChatMsgType::FILE) {
    auto *bubble = new FileBubble(role);
    item->setWidget(bubble);
    bubble->Load(message->GetContent());
  } else if (message->GetMsgType() == ChatMsgType::AUDIO) {
    auto *bubble = new AudioBubble(role);
    item->setWidget(bubble);
    bubble->Load(message->GetContent());
  } else
    item->setWidget(new TextBubble(role, message->GetContent()));
  const bool pending = message->GetMsgId() == 0;
  item->setStatus(pending && message->GetStatus() != SEND_FAILED ? -1 : message->GetStatus());
  if (pending)
    _pendingItems.insert(message->GetUniqueId(), item);
  const auto data = _chat_data;
  connect(item, &ChatItemBase::selectionChanged, this, [this, item, scrollKey, data](bool checked) {
    const auto value = _renderedMessages.value(scrollKey);
    if (!_selecting || data != _chat_data || _renderedItems.value(scrollKey) != item || !value ||
        value->GetMsgId() <= 0)
      return;
    if (checked && _selected.size() >= 50 && !_selected.contains(value->GetMsgId()))
      ShowStatus(tr("每次最多选择 50 条消息"));
    else if (checked)
      _selected.insert(value->GetMsgId(), value);
    else
      _selected.remove(value->GetMsgId());
    UpdateSelection();
  });
  connect(
      item, &ChatItemBase::messageContextMenuRequested, this,
      [this, item, scrollKey, data, pictureBubble](const QPoint &position) {
        // 复用控件后必须读取最新消息，避免重试仍使用发送确认前的旧对象。
        const auto message = _renderedMessages.value(scrollKey);
        if (!message || _renderedItems.value(scrollKey) != item)
          return;
        auto *menu = new MessageMenu(item);
        _messageActions->Bind(menu, message);
        const bool imageMessage = message->GetMsgType() == ChatMsgType::PIC;
        const bool audioMessage = message->GetMsgType() == ChatMsgType::AUDIO;
        const bool fileMessage = message->GetMsgType() == ChatMsgType::FILE;
        connect(menu, &MessageMenu::copyRequested, this, [message, imageMessage, pictureBubble] {
          if (imageMessage && pictureBubble)
            pictureBubble->copyOriginal();
          else if (!imageMessage)
            QGuiApplication::clipboard()->setText(message->GetContent());
        });
        connect(menu, &MessageMenu::placeholderRequested, this,
                [this](const QString &title) { ShowStatus(tr("%1功能暂未实现").arg(title)); });
        connect(menu, &MessageMenu::retryRequested, this, [this, message, data] {
          _sender->RetryMessage(std::static_pointer_cast<TextChatData>(message), data);
        });
        const bool showRetry =
            data && message->GetMsgId() == 0 && message->GetStatus() == SEND_FAILED;
        const bool canRetry = _sender->CanRetry() && (!data || !data->IsGroup() || !imageMessage ||
                                                      TcpMgr::GetInstance()->SupportsGroupImages());
        const MessageMenu::Options options{
            (!imageMessage && !audioMessage && !fileMessage) ||
                (pictureBubble && pictureBubble->hasImage()),
            showRetry,
            canRetry,
            !imageMessage && !audioMessage && !fileMessage,
            !fileMessage && ui->chatEdit->isEnabled(),
            message->GetMsgId() > 0 && ChatStore::instance()->IsReady(),
            !fileMessage && message->GetMsgId() > 0 && ChatStore::instance()->IsReady()};
        menu->Popup(position, options);
      });
  return item;
}

void ChatPage::UpdateChatStatus(const QString &uniqueId, int status)
{
  if (auto *item = _pendingItems.value(uniqueId))
    item->setStatus(status);
  if (status >= 0 && status != SEND_FAILED)
    _pendingItems.remove(uniqueId);
}
void ChatPage::ShowStatus(const QString &message)
{
  ui->chat_status->setText(message);
  ui->chat_status->setVisible(!message.isEmpty());
}

void ChatPage::SaveCurrentDraft()
{
  if (_draftTimer)
    _draftTimer->stop();
  if (!_chat_data || _restoringDraft || _sender->SavingMessages() ||
      _loadingDrafts.contains(_chat_data->GetThreadId()) ||
      !_dirtyDrafts.contains(_chat_data->GetThreadId()))
    return;
  const auto draft = ui->chatEdit->messages();
  _drafts.insert(_chat_data->GetThreadId(), draft);
  PersistDraft(_chat_data->GetThreadId());
}

void ChatPage::PersistDraft(int threadId)
{
  if (!ChatStore::instance()->IsReady())
    return;
  auto draft = _drafts.value(threadId);
  for (auto &part : draft) {
    if (!part.preparation)
      continue;
    if (!part.preparation->ready)
      return;
    part.imageData = part.preparation->bytes;
    part.preparation.reset();
  }
  // 处理失败的图片不写成空图片草稿；其余文字和成功图片照常保留。
  draft.erase(std::remove_if(draft.begin(), draft.end(),
                             [](const MsgInfo &part) {
                               return part.msgFlag == "image" && part.imageData.isEmpty();
                             }),
              draft.end());
  _dirtyDrafts.remove(threadId);
  ChatStore::instance()->SaveDraft(threadId, draft);
}

void ChatPage::UpdateSendButton()
{
  ui->send_btn->setEnabled(_chat_data && ui->chatEdit->isEnabled() && !_sender->SavingMessages() &&
                           !ui->chatEdit->toPlainText().trimmed().isEmpty() &&
                           ui->chatEdit->attachmentsReady());
}

void ChatPage::on_send_btn_clicked()
{
  if (!_chat_data || _sender->SavingMessages() || !ui->chatEdit->isEnabled())
    return;
  if (!ui->chatEdit->attachmentsReady()) {
    ShowStatus(tr("请等待图片处理完成，或删除处理失败的图片"));
    return;
  }
  SaveCurrentDraft();
  ui->chatEdit->setFocus(Qt::OtherFocusReason);
  _sender->Send(ui->chatEdit->messages(), _chat_data);
}

void ChatPage::SetThreadPinned(int threadId, bool pinned)
{
  if (_chat_data && _chat_data->GetThreadId() == threadId)
    ui->chat_settings_panel->SetPinned(pinned);
}

void ChatPage::SetThreadMuted(int threadId, bool muted)
{
  if (_chat_data && _chat_data->GetThreadId() == threadId)
    ui->chat_settings_panel->SetMuted(muted);
}

void ChatPage::SetupSelection()
{
  _selectionBar = new QWidget(this);
  _selectionBar->setObjectName("messageSelectionBar");
  _selectionBar->setAttribute(Qt::WA_StyledBackground);
  auto *layout = new QHBoxLayout(_selectionBar);
  layout->setContentsMargins(16, 6, 16, 6);
  _selectionCount = new QLabel(_selectionBar);
  _selectionCount->setObjectName("messageSelectionCount");
  layout->addWidget(_selectionCount);
  layout->addStretch();
  const auto button = [this, layout](const QString &name, const QString &text) {
    auto *value = new QPushButton(text, _selectionBar);
    value->setObjectName(name);
    layout->addWidget(value);
    return value;
  };
  auto *favorite = button("selectionFavorite", tr("收藏"));
  auto *forward = button("selectionForward", tr("转发"));
  auto *remove = button("selectionDelete", tr("仅本机删除"));
  auto *cancel = button("selectionCancel", tr("取消"));
  _selectionActions = {favorite, forward, remove};
  connect(favorite, &QPushButton::clicked, this,
          [this] { _messageActions->Favorite(SelectedMessages()); });
  connect(forward, &QPushButton::clicked, this,
          [this] { _messageActions->Forward(SelectedMessages()); });
  connect(remove, &QPushButton::clicked, this,
          [this] { _messageActions->DeleteMessages(SelectedMessages()); });
  connect(cancel, &QPushButton::clicked, this, &ChatPage::EndSelection);
  ui->verticalLayout->insertWidget(0, _selectionBar);
  _selectionBar->hide();
}

QVector<std::shared_ptr<ChatDataBase>> ChatPage::SelectedMessages() const
{
  return _selected.values();
}

void ChatPage::BeginSelection(int serverId)
{
  if (!_chat_data || !ChatStore::instance()->IsReady())
    return;
  const auto message = _chat_data->GetMsgMapRef().value(serverId);
  if (!message || serverId <= 0)
    return;
  _selecting = true;
  _selected.clear();
  _selected.insert(serverId, message);
  qApp->installEventFilter(this);
  UpdateSelection();
}

void ChatPage::EndSelection()
{
  _selecting = false;
  _selected.clear();
  qApp->removeEventFilter(this);
  UpdateSelection();
}

void ChatPage::UpdateSelection()
{
  _selectionBar->setVisible(_selecting);
  _selectionCount->setText(tr("已选择 %1 / 50 条").arg(_selected.size()));
  for (auto *button : _selectionActions)
    button->setEnabled(!_selected.isEmpty() && ChatStore::instance()->IsReady());
  for (auto it = _renderedItems.cbegin(); it != _renderedItems.cend(); ++it) {
    const auto message = _renderedMessages.value(it.key());
    if (message)
      it.value()->setSelection(_selecting && message->GetMsgId() > 0,
                               _selected.contains(message->GetMsgId()));
  }
}

void ChatPage::UpdateSettingsRect()
{
  const int top = ui->title_wid->mapTo(this, QPoint(0, ui->title_wid->height())).y();
  ui->chat_settings_panel->SetAvailableRect(QRect(0, top, width(), qMax(0, height() - top)));
}

void ChatPage::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
  UpdateSettingsRect();
}

void ChatPage::showEvent(QShowEvent *event)
{
  QWidget::showEvent(event);
  UpdateSettingsRect();
}

void ChatPage::hideEvent(QHideEvent *event)
{
  if (_voiceMessage)
    _voiceMessage->reject();
  EndSelection();
  for (auto *menu : findChildren<MessageMenu *>())
    menu->hide();
  _messageActions->CloseReader();
  ui->call_btn->menu()->close();
  ui->chat_settings_panel->Close(false);
  QWidget::hideEvent(event);
}

void ChatPage::paintEvent(QPaintEvent *)
{
  QStyleOption option;
  option.initFrom(this);
  QPainter painter(this);
  style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}
