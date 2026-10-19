#include "MessageTextEdit.h"
#include "common/attachmentpreparer.h"
#include "common/clientpreferences.h"
#include "common/emojicatalog.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QUrl>

namespace {
constexpr int kImageDataProperty = QTextFormat::UserProperty + 1;
} // namespace

MessageTextEdit::MessageTextEdit(QWidget *parent) : QTextEdit(parent)
{
  setAcceptRichText(false);
  connect(ClientPreferences::instance(), &ClientPreferences::changed, this,
          &MessageTextEdit::UpdateSendShortcut);
  UpdateSendShortcut();
  connect(AttachmentPreparer::instance(), &AttachmentPreparer::prepared, this,
          [this](const QString &id) {
            if (const auto attachment = _attachments.value(id)) {
              updatePreview(attachment);
              if (!attachment->error.isEmpty())
                emit attachmentError(attachment->error);
              emit attachmentsChanged();
            }
          });
}
MessageTextEdit::~MessageTextEdit() = default;

void MessageTextEdit::changeEvent(QEvent *event)
{
  QTextEdit::changeEvent(event);
  if (event->type() == QEvent::PaletteChange)
    for (const auto &attachment : _attachments)
      if (attachment && attachment->preview.isNull())
        updatePreview(attachment);
}

void MessageTextEdit::insertQuote(const QString &sender, const QString &text)
{
  if (!isEnabled() || isReadOnly())
    return;
  QString excerpt = text.left(1024);
  if (!excerpt.isEmpty() && excerpt.at(excerpt.size() - 1).isHighSurrogate())
    excerpt.chop(1);
  const bool shortened = excerpt.size() < text.size();
  excerpt.replace("\r\n", "\n").replace('\r', '\n');
  excerpt.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
  excerpt.replace(QChar::LineSeparator, QLatin1Char('\n'));
  auto lines = excerpt.split('\n');
  const bool moreLines = lines.size() > 8;
  lines = lines.mid(0, 8);
  QString author = sender.simplified().left(64);
  if (!author.isEmpty() && author.at(author.size() - 1).isHighSurrogate())
    author.chop(1);
  QString quote = QStringLiteral("> %1：\n").arg(author);
  for (const auto &line : lines)
    quote += QStringLiteral("> ") + line + QLatin1Char('\n');
  if (shortened || moreLines)
    quote += QStringLiteral("> …\n");
  quote += QLatin1Char('\n');
  // Insert before the draft, without replacing its current selection or attachments.
  QTextCursor cursor(document());
  cursor.beginEditBlock();
  cursor.insertText(quote);
  cursor.endEditBlock();
  setTextCursor(cursor);
  setFocus(Qt::OtherFocusReason);
}

QVector<MsgInfo> MessageTextEdit::messages() const
{
  QVector<MsgInfo> result;
  QString text;
  const auto appendText = [&] {
    if (text.isEmpty())
      return;
    MsgInfo message;
    message.msgFlag = "text";
    message.content = text;
    result.append(message);
    text.clear();
  };
  // 按文档顺序读取图片，删除、撤销和移动后也不会错配附件。
  for (auto block = document()->begin(); block.isValid(); block = block.next()) {
    if (block != document()->begin())
      text += QLatin1Char('\n');
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const auto fragment = it.fragment();
      if (!fragment.isValid())
        continue;
      if (!fragment.charFormat().isImageFormat()) {
        text += fragment.text();
        continue;
      }
      const auto token = fragment.charFormat().property(EmojiCatalog::TokenProperty).toString();
      if (!token.isEmpty()) {
        text += token.repeated(fragment.length());
        continue;
      }
      appendText();
      MsgInfo message;
      message.msgFlag = "image";
      message.content = fragment.charFormat().toImageFormat().name();
      message.preparation = _attachments.value(message.content);
      message.imageData = message.preparation
                              ? message.preparation->bytes
                              : fragment.charFormat().property(kImageDataProperty).toByteArray();
      for (int index = 0; index < fragment.length(); ++index)
        result.append(message);
    }
  }
  appendText();
  return result;
}

bool MessageTextEdit::attachmentsReady() const
{
  // 只检查仍在文档中的图片，已删除但留在撤销历史中的任务不阻止发送。
  for (auto block = document()->begin(); block.isValid(); block = block.next())
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const auto format = it.fragment().charFormat();
      if (!format.isImageFormat() ||
          !format.property(EmojiCatalog::TokenProperty).toString().isEmpty())
        continue;
      const auto attachment = _attachments.value(format.toImageFormat().name());
      if (attachment && (!attachment->ready || !attachment->error.isEmpty()))
        return false;
    }
  return true;
}

void MessageTextEdit::clear()
{
  _attachments.clear();
  QTextEdit::clear();
}

void MessageTextEdit::setMessages(const QVector<MsgInfo> &messages)
{
  clear();
  for (const auto &message : messages) {
    if (message.msgFlag == "image") {
      if (message.preparation)
        insertPrepared(message.preparation);
      else
        insertImageData(message.imageData);
    } else if (message.msgFlag == "text") {
      auto cursor = textCursor();
      EmojiCatalog::InsertText(cursor, message.content);
      setTextCursor(cursor);
    }
  }
}

void MessageTextEdit::dragEnterEvent(QDragEnterEvent *event)
{
  if (event->source() != this && canInsertFromMimeData(event->mimeData()))
    event->acceptProposedAction();
  else
    event->ignore();
}
void MessageTextEdit::dropEvent(QDropEvent *event)
{
  if (!canInsertFromMimeData(event->mimeData())) {
    event->ignore();
    return;
  }
  setTextCursor(cursorForPosition(event->position().toPoint()));
  insertFromMimeData(event->mimeData());
  event->acceptProposedAction();
}
void MessageTextEdit::keyPressEvent(QKeyEvent *event)
{
  const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
  const bool sendKey =
      _controlEnter ? modifiers == Qt::ControlModifier : modifiers == Qt::NoModifier;
  if ((event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) && sendKey && !_composing &&
      !isReadOnly() && isEnabled()) {
    event->accept();
    if (!event->isAutoRepeat())
      emit send();
    return;
  }
  QTextEdit::keyPressEvent(event);
}

void MessageTextEdit::UpdateSendShortcut()
{
  _controlEnter =
      ClientPreferences::instance()->Current().sendKey == ClientPreferences::SendKey::ControlEnter;
  setPlaceholderText(_controlEnter ? tr("输入消息，按 Ctrl+Enter 发送")
                                   : tr("输入消息，按 Enter 发送"));
  setToolTip(_controlEnter ? tr("Ctrl+Enter 发送；Enter / Shift+Enter 换行")
                           : tr("Enter 发送；Shift+Enter / Ctrl+Enter 换行"));
}

void MessageTextEdit::inputMethodEvent(QInputMethodEvent *event)
{
  _composing = !event->preeditString().isEmpty();
  QTextEdit::inputMethodEvent(event);
}

void MessageTextEdit::insertFileFromUrl(const QStringList &urls)
{
  for (const auto &path : urls)
    insertPrepared(AttachmentPreparer::instance()->FromFile(path));
}

void MessageTextEdit::insertImage(const QImage &image)
{
  insertPrepared(AttachmentPreparer::instance()->FromImage(image));
}

void MessageTextEdit::insertEmoji(const QString &id)
{
  if (const auto *emoji = EmojiCatalog::FindById(id)) {
    auto cursor = textCursor();
    EmojiCatalog::InsertEmoji(cursor, *emoji);
    setTextCursor(cursor);
  }
}

void MessageTextEdit::insertImageData(const QByteArray &bytes)
{
  insertPrepared(AttachmentPreparer::instance()->FromBytes(bytes));
}

void MessageTextEdit::updatePreview(const std::shared_ptr<PreparedAttachment> &attachment)
{
  QImage canvas(QSize(120, 80), QImage::Format_ARGB32_Premultiplied);
  canvas.fill(Qt::transparent);
  QPainter painter(&canvas);
  if (!attachment->preview.isNull()) {
    painter.drawImage(
        QPoint((120 - attachment->preview.width()) / 2, (80 - attachment->preview.height()) / 2),
        attachment->preview);
  } else {
    painter.fillRect(canvas.rect(), palette().color(QPalette::Base));
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(canvas.rect(), Qt::AlignCenter,
                     attachment->error.isEmpty() ? tr("图片处理中") : tr("图片处理失败"));
  }
  painter.end();
  document()->addResource(QTextDocument::ImageResource, QUrl(attachment->id), canvas);
  document()->markContentsDirty(0, document()->characterCount());
  viewport()->update();
}

void MessageTextEdit::insertPrepared(const std::shared_ptr<PreparedAttachment> &attachment)
{
  if (attachment->ready && !attachment->error.isEmpty()) {
    emit attachmentError(attachment->error);
    return;
  }
  _attachments.insert(attachment->id, attachment);
  updatePreview(attachment);
  QTextImageFormat format;
  format.setName(attachment->id);
  format.setWidth(120);
  format.setHeight(80);
  auto cursor = textCursor();
  cursor.insertImage(format);
  cursor.setCharFormat(QTextCharFormat());
  setTextCursor(cursor);
}

bool MessageTextEdit::canInsertFromMimeData(const QMimeData *source) const
{
  return source && (source->hasImage() || source->hasUrls() || source->hasText());
}
void MessageTextEdit::insertFromMimeData(const QMimeData *source)
{
  if (!source)
    return;
  QStringList localFiles;
  for (const auto &url : source->urls())
    if (url.isLocalFile())
      localFiles.append(url.toLocalFile());
  if (!localFiles.isEmpty()) {
    insertFileFromUrl(localFiles);
    return;
  }
  if (source->hasImage()) {
    QImage image = qvariant_cast<QImage>(source->imageData());
    if (image.isNull())
      image = qvariant_cast<QPixmap>(source->imageData()).toImage();
    insertImage(image);
    return;
  }
  // 外部富文本转成普通文字，避免插入无法上传的远程图片占位符。
  if (source->hasText()) {
    auto cursor = textCursor();
    EmojiCatalog::InsertText(cursor, source->text());
    setTextCursor(cursor);
  }
}
