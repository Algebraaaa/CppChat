#include "chatpage.h"
#include "ui_chatpage.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "widgets/TextBubble.h"
#include "widgets/chatitembase.h"
#include <QFileDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QPainter>
#include <QStyleOption>
#include <QTextCursor>
#include <QTimer>
#include <QUuid>

namespace {
// 服务端回包同时包含 content 和 msg_content，并可能将中文转为 \uXXXX。
// 为两份正文和回包字段预留空间，避免请求能发送、回包却超过服务端 2KB 上限。
int escapedJsonSize(const QString &text)
{
  int size = 0;
  for (const auto ch : text) {
    if (ch.unicode() < 0x20 || ch.unicode() > 0x7e) size += 6;
    else if (ch == QLatin1Char('"') || ch == QLatin1Char('\\')) size += 2;
    else ++size;
  }
  return size;
}

QByteArray textPacket(const QString &text, const QString &uniqueId, int sender, int receiver, int thread)
{
  const QJsonArray messages{QJsonObject{{"content", text}, {"unique_id", uniqueId}}};
  return QJsonDocument(QJsonObject{{"fromuid", sender}, {"touid", receiver},
      {"thread_id", thread}, {"text_array", messages}}).toJson(QJsonDocument::Compact);
}
}

ChatPage::ChatPage(QWidget *parent) : QWidget(parent), ui(new Ui::ChatPage)
{
  ui->setupUi(this);
  ui->send_btn->SetState("normal", "hover", "press");
  // 原来的“接收”是本地造消息的测试按钮；实际接收由 TCP 通知驱动。
  ui->receive_btn->hide();
  ui->emo_lb->SetState("normal", "hover", "press", "normal", "hover", "press");
  ui->file_lb->SetState("normal", "hover", "press", "normal", "hover", "press");
  _statusLabel = new QLabel(this);
  _statusLabel->setObjectName("chat_status");
  _statusLabel->setWordWrap(true);
  qobject_cast<QVBoxLayout *>(layout())->insertWidget(1, _statusLabel);
  connect(ui->send_btn, &QPushButton::clicked, this, &ChatPage::on_send_btn_clicked);
  connect(ui->chatEdit, &MessageTextEdit::send, this, &ChatPage::on_send_btn_clicked);
  connect(ui->file_lb, &ClickedLabel::clicked, this, [this] {
    const auto files = QFileDialog::getOpenFileNames(this, tr("选择本地预览文件"));
    ui->chatEdit->insertFileFromUrl(files);
    if (!files.isEmpty()) ShowStatus(tr("图片和文件目前仅支持本地预览，尚不能发送给好友"));
  });
  connect(ui->emo_lb, &ClickedLabel::clicked, this, [this] { ui->chatEdit->insertPlainText(QString::fromUtf8("🙂")); });
  Reset();
}
ChatPage::~ChatPage() { delete ui; }

void ChatPage::Reset()
{
  _chat_data.reset();
  _pendingItems.clear();
  _drafts.clear();
  ui->chat_data_list->removeAllItem();
  ui->chatEdit->clear();
  ui->chatEdit->setEnabled(false);
  ui->send_btn->setEnabled(false);
  ui->title_label->setText(tr("选择好友开始聊天"));
  ShowStatus({});
}

void ChatPage::SetChatData(std::shared_ptr<ChatThreadData> data)
{
  if (!data) { Reset(); return; }
  const bool changed = !_chat_data || _chat_data->GetThreadId() != data->GetThreadId();
  if (changed && _chat_data) _drafts.insert(_chat_data->GetThreadId(), ui->chatEdit->toPlainText());
  _chat_data = data;
  if (changed) ui->chatEdit->setPlainText(_drafts.value(data->GetThreadId()));
  const auto user = UserMgr::GetInstance()->GetFriendById(data->GetOtherId());
  ui->title_label->setText(user ? (user->_back.isEmpty() ? user->_name : user->_back)
                           : tr("用户 %1").arg(data->GetOtherId()));
  ui->chatEdit->setEnabled(true);
  ui->send_btn->setEnabled(true);
  _pendingItems.clear();
  ui->chat_data_list->removeAllItem();
  for (const auto &message : data->GetMsgMapRef()) AppendChatMsg(message);
  for (const auto &message : data->GetPendingMessages()) AppendChatMsg(message);
}

void ChatPage::AppendChatMsg(std::shared_ptr<ChatDataBase> message)
{
  if (!message || message->GetMsgType() != ChatMsgType::TEXT) return;
  const auto mgr = UserMgr::GetInstance();
  const bool self = message->GetSendUid() == mgr->GetUid();
  const auto user = self ? mgr->GetUserInfo() : mgr->GetFriendById(message->GetSendUid());
  const auto role = self ? ChatRole::Self : ChatRole::Other;
  auto *item = new ChatItemBase(role);
  item->setUserName(user ? user->_name : tr("用户 %1").arg(message->GetSendUid()));
  QPixmap icon(user ? user->_icon : QString());
  if (icon.isNull()) icon.load(":/res/head_1.jpg");
  item->setUserIcon(icon);
  item->setWidget(new TextBubble(role, message->GetContent()));
  const bool pending = message->GetMsgId() == 0;
  item->setStatus(pending && message->GetStatus() != SEND_FAILED ? -1 : message->GetStatus());
  if (pending) _pendingItems.insert(message->GetUniqueId(), item);
  ui->chat_data_list->appendChatItem(item);
}

void ChatPage::UpdateChatStatus(const QString &uniqueId, int status)
{
  if (auto *item = _pendingItems.value(uniqueId)) item->setStatus(status);
  if (status != SEND_FAILED) _pendingItems.remove(uniqueId);
}
void ChatPage::ShowStatus(const QString &message)
{ _statusLabel->setText(message); _statusLabel->setVisible(!message.isEmpty()); }

void ChatPage::on_send_btn_clicked()
{
  if (!_chat_data) return;
  const auto tcp = TcpMgr::GetInstance();
  if (!tcp->IsConnected()) { ShowStatus(tr("连接已断开，请重新登录")); return; }
  const QString text = ui->chatEdit->toPlainText();
  if (text.trimmed().isEmpty()) return;
  if (text.contains(QChar::ObjectReplacementCharacter)) {
    ShowStatus(tr("当前只能发送文本，请先移除输入框中的图片或文件预览"));
    return;
  }

  // 每个 JSON 包都控制在服务端 2048 字节上限内，按 Unicode 码点分割，避免拆开代理对。
  QStringList parts;
  QString part;
  const QString sampleId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const int sender = UserMgr::GetInstance()->GetUid();
  const int receiver = _chat_data->GetOtherId();
  const int thread = _chat_data->GetThreadId();
  for (int i = 0; i < text.size(); ++i) {
    QString symbol(text[i]);
    if (text[i].isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate()) symbol += text[++i];
    const QString candidate = part + symbol;
    if (!part.isEmpty() && (escapedJsonSize(candidate) > 600
        || textPacket(candidate, sampleId, sender, receiver, thread).size() > 1800)) {
      parts.append(part);
      part.clear();
    }
    part += symbol;
  }
  if (!part.isEmpty()) parts.append(part);
  ui->chatEdit->clear();
  _drafts.remove(thread);
  ShowStatus({});
  const auto data = _chat_data;
  for (const auto &content : parts) {
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto message = std::make_shared<TextChatData>(id, thread, ChatFormType::PRIVATE,
        ChatMsgType::TEXT, content, sender, UN_READ);
    data->AppendUnRspMsg(id, message);
    AppendChatMsg(message);
    emit tcp->sig_send_data(ID_TEXT_CHAT_MSG_REQ, textPacket(content, id, sender, receiver, thread));
    // 回包按 unique_id 将待确认消息移入正式记录，计时器仅标记仍未确认的消息。
    QTimer::singleShot(15000, this, [this, data, message, id] {
      if (!data->GetMsgUnRspRef().contains(id)) return;
      message->SetStatus(SEND_FAILED);
      if (_chat_data == data) {
        UpdateChatStatus(id, SEND_FAILED);
        ShowStatus(tr("部分消息未收到确认，请检查连接；服务端晚到的确认仍会更新状态"));
      }
    });
  }
  emit sig_messages_changed(thread);
}

void ChatPage::paintEvent(QPaintEvent *)
{
  QStyleOption option;
  option.initFrom(this);
  QPainter painter(this);
  style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}
