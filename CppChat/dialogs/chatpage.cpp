#include "chatpage.h"
#include "chathistorydialog.h"
#include "common/attachmentpreparer.h"
#include "common/chatstore.h"
#include "messageactions.h"
#include "messagemenu.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/messagesender.h"
#include "ui_chatpage.h"
#include "voicecalldialog.h"
#include "widgets/chatitembase.h"
#include "widgets/emojipicker.h"
#include "widgets/picturebubble.h"
#include "widgets/screencapture.h"
#include "widgets/textbubble.h"
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
  _messageActions = new MessageActions(this);
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
  ui->chatEdit->setPlaceholderText(tr("输入消息，按 Enter 发送"));
  ui->emo_btn->setToolTip(tr("表情"));
  ui->file_btn->setToolTip(tr("发送图片"));
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
    if (QMessageBox::question(
            this, tr("清空本机聊天记录"),
            tr("清空本机已确认的聊天记录？服务器记录、图片缓存和待确认消息会保留。")) !=
        QMessageBox::Yes)
      return;
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
  connect(ui->file_btn, &QPushButton::clicked, this, [this] {
    if (!ui->chatEdit->isEnabled())
      return;
    if (!_chat_data || !TcpMgr::GetInstance()->IsConnected()) {
      ShowStatus(tr("请先选择会话并连接聊天服务器"));
      return;
    }
    if (_chat_data->IsGroup()) {
      ShowStatus(tr("群聊暂支持文字和表情，图片发送尚未开放"));
      return;
    }
    if (!UserMgr::GetInstance()->CheckFriendById(_chat_data->GetOtherId())) {
      ShowStatus(errorCodeMessage(FriendNotFound));
      return;
    }
    const auto files = QFileDialog::getOpenFileNames(
        this, tr("发送图片"), {}, tr("图片 (*.png *.jpg *.jpeg *.gif *.webp *.bmp)"));
    ui->chatEdit->insertFileFromUrl(files);
    ui->chatEdit->setFocus();
  });
  connect(_sender, &MessageSender::savingChanged, this, [this](bool saving) {
    ui->chatEdit->setEnabled(!saving && _chat_data &&
                             !_loadingDrafts.contains(_chat_data->GetThreadId()));
    UpdateSendButton();
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
  for (auto *button : {ui->voice_transcribe_btn, ui->voice_message_btn}) {
    connect(button, &QPushButton::clicked, this,
            [this, button] { ShowStatus(tr("%1功能暂未开放").arg(button->accessibleName())); });
  }
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
  const bool canCall = _chat_data && !_chat_data->IsGroup();
  if (!canCall)
    ui->call_btn->menu()->close();
  ui->call_btn->setEnabled(canCall);
  ui->call_btn->setToolTip(_chat_data && _chat_data->IsGroup() ? tr("暂不支持群通话")
                                                               : tr("语音/视频通话"));
  ui->voice_transcribe_btn->setEnabled(bool(_chat_data));
  ui->voice_message_btn->setEnabled(bool(_chat_data));
}

void ChatPage::StartScreenCapture()
{
  if (_screenCapture || !ui->chatEdit->isEnabled())
    return;
  if (!_chat_data || !TcpMgr::GetInstance()->IsConnected()) {
    ShowStatus(tr("请先选择会话并连接聊天服务器"));
    return;
  }
  if (_chat_data->IsGroup()) {
    ShowStatus(tr("群聊暂支持文字和表情，图片发送尚未开放"));
    return;
  }
  if (!UserMgr::GetInstance()->CheckFriendById(_chat_data->GetOtherId())) {
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
        !UserMgr::GetInstance()->CheckFriendById(conversation->GetOtherId())) {
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

void ChatPage::Reset()
{
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
  ShowStatus({});
}

void ChatPage::SetChatData(std::shared_ptr<ChatThreadData> data)
{
  if (!data) {
    Reset();
    return;
  }
  const bool changed = !_chat_data || _chat_data->GetThreadId() != data->GetThreadId();
  ui->file_btn->setEnabled(!data->IsGroup());
  ui->file_btn->setToolTip(data->IsGroup() ? tr("群图片尚未开放") : tr("发送图片"));
  if (changed) {
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
        (message->GetMsgType() != ChatMsgType::TEXT && message->GetMsgType() != ChatMsgType::PIC))
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
}

void ChatPage::RemoveChatThread(int threadId)
{
  _drafts.remove(threadId);
  _dirtyDrafts.remove(threadId);
  _sender->RemoveThread(threadId);
  if (!_chat_data || _chat_data->GetThreadId() != threadId)
    return;
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
  } else
    item->setWidget(new TextBubble(role, message->GetContent()));
  const bool pending = message->GetMsgId() == 0;
  item->setStatus(pending && message->GetStatus() != SEND_FAILED ? -1 : message->GetStatus());
  if (pending)
    _pendingItems.insert(message->GetUniqueId(), item);
  const auto data = _chat_data;
  connect(item, &ChatItemBase::messageContextMenuRequested, this,
          [this, item, scrollKey, data, pictureBubble](const QPoint &position) {
            // 复用控件后必须读取最新消息，避免重试仍使用发送确认前的旧对象。
            const auto message = _renderedMessages.value(scrollKey);
            if (!message || _renderedItems.value(scrollKey) != item)
              return;
            auto *menu = new MessageMenu(item);
            _messageActions->Bind(menu, message);
            const bool imageMessage = message->GetMsgType() == ChatMsgType::PIC;
            connect(menu, &MessageMenu::copyRequested, this,
                    [message, imageMessage, pictureBubble] {
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
            const bool canRetry = _sender->CanRetry();
            const MessageMenu::Options options{
                !imageMessage || (pictureBubble && pictureBubble->hasImage()),
                showRetry,
                canRetry,
                !imageMessage,
                ui->chatEdit->isEnabled(),
                message->GetMsgId() > 0 && ChatStore::instance()->IsReady()};
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
  _sender->Send(ui->chatEdit->messages(), _chat_data);
}

void ChatPage::SetThreadPinned(int threadId, bool pinned)
{
  if (_chat_data && _chat_data->GetThreadId() == threadId)
    ui->chat_settings_panel->SetPinned(pinned);
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
