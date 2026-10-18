#include "messageactions.h"
#include "common/chatstore.h"
#include "messagemenu.h"
#include "network/usermgr.h"
#include "services/messagesnapshotservice.h"
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

MessageActions::MessageActions(QWidget *owner) : QObject(owner), _owner(owner)
{
  _snapshots = new MessageSnapshotService(this);
  connect(ChatStore::instance(), &ChatStore::localMessageDeleted, this, [this](int thread, int id) {
    if (_data && _data->GetThreadId() == thread && _readerMessage == id && _reader)
      _reader->close();
  });
  connect(ChatStore::instance(), &ChatStore::localMessagesDeleted, this,
          [this](int thread, const QVector<int> &ids) {
            if (_data && _data->GetThreadId() == thread && ids.contains(_readerMessage) && _reader)
              _reader->close();
          });
}

void MessageActions::SetConversation(std::shared_ptr<ChatThreadData> data)
{
  const int uid = UserMgr::GetInstance()->GetUid();
  const auto generation = ChatStore::instance()->Generation();
  if (_data != data || _uid != uid || _generation != generation) {
    _snapshots->Cancel();
    CloseReader();
    _deleting.clear();
  }
  _data = data;
  _uid = uid;
  _generation = generation;
}

void MessageActions::CloseReader()
{
  if (_reader)
    _reader->close();
  _reader.clear();
}

bool MessageActions::IsCurrent(const std::shared_ptr<ChatThreadData> &data,
                               const std::shared_ptr<ChatDataBase> &message) const
{
  return _owner->isVisible() && data && _data == data && message &&
         message->GetThreadId() == data->GetThreadId() &&
         _uid == UserMgr::GetInstance()->GetUid() &&
         _generation == ChatStore::instance()->Generation() &&
         UserMgr::GetInstance()->GetChatThreadByThreadId(data->GetThreadId()) == data;
}

QString MessageActions::Sender(int uid) const
{
  if (uid == _uid)
    return tr("我");
  const auto user = UserMgr::GetInstance()->GetFriendById(uid);
  if (user)
    return user->_back.trimmed().isEmpty() ? user->_name : user->_back;
  return _data && _data->IsGroup() ? _data->MemberName(uid) : tr("用户 %1").arg(uid);
}

void MessageActions::Bind(MessageMenu *menu, std::shared_ptr<ChatDataBase> message)
{
  const auto data = _data;
  connect(menu, &MessageMenu::readRequested, this, [this, data, message] {
    if (IsCurrent(data, message) && message->GetMsgType() == ChatMsgType::TEXT)
      Read(message);
  });
  connect(menu, &MessageMenu::quoteRequested, this, [this, data, message] {
    if (IsCurrent(data, message) && message->GetMsgType() == ChatMsgType::TEXT)
      emit quoteRequested(data->GetThreadId(), Sender(message->GetSendUid()),
                          message->GetContent());
  });
  connect(menu, &MessageMenu::deleteRequested, this, [this, data, message] {
    if (IsCurrent(data, message) && message->GetMsgId() > 0)
      Delete(message);
  });
  connect(menu, &MessageMenu::favoriteRequested, this, [this, data, message] {
    if (IsCurrent(data, message))
      Favorite({message});
  });
  connect(menu, &MessageMenu::forwardRequested, this, [this, data, message] {
    if (IsCurrent(data, message))
      Forward({message});
  });
  connect(menu, &MessageMenu::searchRequested, this, [this, data, message] {
    if (IsCurrent(data, message))
      emit searchRequested(data->GetThreadId());
  });
  connect(menu, &MessageMenu::selectRequested, this, [this, data, message] {
    if (IsCurrent(data, message) && message->GetMsgId() > 0)
      emit selectionRequested(message->GetMsgId());
  });
}

void MessageActions::Favorite(const QVector<std::shared_ptr<ChatDataBase>> &messages)
{
  Collect(messages, true);
}

void MessageActions::Forward(const QVector<std::shared_ptr<ChatDataBase>> &messages)
{
  Collect(messages, false);
}

void MessageActions::Collect(const QVector<std::shared_ptr<ChatDataBase>> &messages, bool favorite)
{
  const auto data = _data;
  if (!data || messages.isEmpty() || messages.size() > 50)
    return;
  QVector<StoredFavorite> values;
  for (const auto &message : messages) {
    if (!IsCurrent(data, message) || message->GetMsgId() <= 0)
      return;
    StoredFavorite value;
    value.threadId = message->GetThreadId();
    value.serverId = message->GetMsgId();
    value.senderUid = message->GetSendUid();
    value.type = static_cast<int>(message->GetMsgType());
    value.senderName = Sender(value.senderUid);
    value.content = message->GetContent();
    values.append(value);
  }
  const QPointer<MessageActions> guard(this);
  const auto generation = _generation;
  const int thread = data->GetThreadId();
  const bool started = _snapshots->Collect(values, [guard, generation, thread,
                                                    favorite](QVector<StoredFavorite> snapshots,
                                                              QString error) {
    if (!guard || ChatStore::instance()->Generation() != generation)
      return;
    if (!error.isEmpty()) {
      emit guard->status(thread, error);
      return;
    }
    if (!favorite) {
      emit guard->forwardRequested(snapshots);
      return;
    }
    ChatStore::instance()->SaveFavorites(snapshots, [guard, generation,
                                                     thread](const ChatStoreResult &r) {
      if (guard && ChatStore::instance()->Generation() == generation)
        emit guard->status(thread, r.error.isEmpty() ? guard->tr("已保存到本机收藏")
                                                     : guard->tr("收藏保存失败，原消息仍然保留"));
    });
  });
  emit status(thread, started ? tr("正在准备消息内容…") : tr("上一项操作尚未完成，请稍后重试"));
}

void MessageActions::Read(const std::shared_ptr<ChatDataBase> &message)
{
  if (_reader)
    _reader->close();
  auto *dialog = new QDialog(_owner);
  dialog->setObjectName("messageReader");
  dialog->setProperty("groupDialog", true);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("放大阅读 · %1").arg(Sender(message->GetSendUid())));
  dialog->resize(
      QSize(640, 480).boundedTo(_owner->screen()->availableGeometry().size() - QSize(40, 60)));
  auto *layout = new QVBoxLayout(dialog);
  auto *text = new QPlainTextEdit(dialog);
  text->setObjectName("messageReaderText");
  text->setReadOnly(true);
  text->setPlainText(message->GetContent());
  // A local font rule takes precedence over the application's QWidget font-size.
  text->setStyleSheet(QStringLiteral("font-size: 18pt;"));
  layout->addWidget(text, 1);
  auto *buttons = new QDialogButtonBox(dialog);
  auto *copy = buttons->addButton(tr("复制全文"), QDialogButtonBox::ActionRole);
  auto *smaller = buttons->addButton(tr("缩小"), QDialogButtonBox::ActionRole);
  auto *larger = buttons->addButton(tr("放大"), QDialogButtonBox::ActionRole);
  buttons->addButton(tr("关闭"), QDialogButtonBox::RejectRole);
  connect(copy, &QPushButton::clicked, text,
          [text] { QApplication::clipboard()->setText(text->toPlainText()); });
  connect(smaller, &QPushButton::clicked, text, [text] {
    const auto size = qMax(10.0, text->font().pointSizeF() - 1);
    text->setStyleSheet(QStringLiteral("font-size: %1pt;").arg(size));
  });
  connect(larger, &QPushButton::clicked, text, [text] {
    const auto size = qMin(40.0, text->font().pointSizeF() + 1);
    text->setStyleSheet(QStringLiteral("font-size: %1pt;").arg(size));
  });
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  layout->addWidget(buttons);
  _reader = dialog;
  _readerMessage = message->GetMsgId();
  dialog->show();
}

void MessageActions::Delete(const std::shared_ptr<ChatDataBase> &message)
{
  DeleteMessages({message});
}

void MessageActions::DeleteMessages(const QVector<std::shared_ptr<ChatDataBase>> &messages)
{
  const auto data = _data;
  if (!data || !ChatStore::instance()->IsReady() || messages.isEmpty() || messages.size() > 50)
    return;
  const int thread = data->GetThreadId();
  QVector<int> ids;
  QStringList keys;
  for (const auto &message : messages) {
    if (!IsCurrent(data, message) || message->GetMsgId() <= 0)
      return;
    const QString key = QString::number(thread) + ':' + QString::number(message->GetMsgId());
    if (_deleting.contains(key))
      return;
    ids.append(message->GetMsgId());
    keys.append(key);
  }
  QMessageBox confirmation(
      QMessageBox::Question, tr("删除本机消息"),
      tr("删除选中的 %1 条本机消息？\n服务器和其他设备上的消息会保留。").arg(ids.size()),
      QMessageBox::NoButton, _owner);
  confirmation.setObjectName("deleteLocalMessageConfirm");
  auto *remove = confirmation.addButton(tr("仅从本机删除"), QMessageBox::DestructiveRole);
  auto *cancel = confirmation.addButton(tr("取消"), QMessageBox::RejectRole);
  confirmation.setDefaultButton(cancel);
  confirmation.exec();
  if (confirmation.clickedButton() != remove)
    return;
  for (const auto &message : messages)
    if (!IsCurrent(data, message))
      return;
  for (const auto &key : keys)
    _deleting.insert(key);
  const QPointer<MessageActions> guard(this);
  const auto generation = _generation;
  ChatStore::instance()->DeleteLocalMessages(
      thread, ids, [guard, generation, keys, thread](const ChatStoreResult &result) {
        if (!guard || ChatStore::instance()->Generation() != generation)
          return;
        for (const auto &key : keys)
          guard->_deleting.remove(key);
        if (!result.error.isEmpty())
          emit guard->status(thread, guard->tr("删除失败，消息仍然保留，请检查本地存储后重试"));
      });
}
