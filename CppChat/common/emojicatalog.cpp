#include "emojicatalog.h"
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QUrl>

const QVector<EmojiEntry> &EmojiCatalog::Entries()
{
  static const QVector<EmojiEntry> entries = [] {
    QVector<EmojiEntry> result;
    for (const auto &path : {QStringLiteral(":/emojis/qq/catalog.json"),
                             QStringLiteral(":/emojis/telegram/catalog.json")}) {
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly))
        continue;
      for (const auto &value : QJsonDocument::fromJson(file.readAll()).array()) {
        const auto object = value.toObject();
        EmojiEntry emoji{object.value("id").toString(),
                         object.value("name").toString(),
                         object.value("token").toString(),
                         object.value("category").toString(),
                         {},
                         object.value("resource").toString(),
                         object.value("pack").toString(QStringLiteral("qq"))};
        for (const auto &alias : object.value("aliases").toArray())
          emoji.aliases.append(alias.toString());
        if (!emoji.id.isEmpty() && QFile::exists(emoji.resource))
          result.append(emoji);
      }
    }
    return result;
  }();
  return entries;
}

const EmojiEntry *EmojiCatalog::FindById(const QString &id)
{
  for (const auto &emoji : Entries())
    if (emoji.id == id)
      return &emoji;
  return nullptr;
}

const EmojiEntry *EmojiCatalog::FindByToken(const QString &token)
{
  for (const auto &emoji : Entries())
    if (emoji.token == token)
      return &emoji;
  return nullptr;
}

void EmojiCatalog::InsertEmoji(QTextCursor &cursor, const EmojiEntry &emoji)
{
  const QImage image(emoji.resource);
  if (image.isNull()) {
    cursor.insertText(emoji.token, QTextCharFormat());
    return;
  }
  const QUrl url("qrc" + emoji.resource);
  cursor.document()->addResource(QTextDocument::ImageResource, url, image);
  QTextImageFormat format;
  format.setName(url.toString());
  format.setWidth(28);
  format.setHeight(28);
  format.setVerticalAlignment(QTextCharFormat::AlignMiddle);
  format.setProperty(TokenProperty, emoji.token);
  cursor.insertImage(format);
  // 后续输入的普通文字不能继承表情标记。
  cursor.setCharFormat(QTextCharFormat());
}

void EmojiCatalog::InsertText(QTextCursor &cursor, const QString &text)
{
  static const QRegularExpression pattern(
      QStringLiteral(R"(\[[^\[\]\r\n]{1,%1}\])").arg(MaximumTokenLength - 2));
  auto matches = pattern.globalMatch(text);
  qsizetype position = 0;
  while (matches.hasNext()) {
    const auto match = matches.next();
    const auto *found = FindByToken(match.captured());
    if (!found)
      continue;
    cursor.insertText(text.mid(position, match.capturedStart() - position), QTextCharFormat());
    InsertEmoji(cursor, *found);
    position = match.capturedEnd();
  }
  cursor.insertText(text.mid(position), QTextCharFormat());
}
