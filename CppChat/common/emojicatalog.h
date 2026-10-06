#ifndef EMOJICATALOG_H
#define EMOJICATALOG_H

#include <QString>
#include <QStringList>
#include <QTextFormat>
#include <QVector>

class QTextCursor;

struct EmojiEntry
{
  QString id;
  QString name;
  QString token;
  QString category;
  QStringList aliases;
  QString resource;
  QString pack;
};

namespace EmojiCatalog {
// 内联表情仍属于文字消息，这个属性保存它在服务端传输的文字标记。
constexpr int TokenProperty = QTextFormat::UserProperty + 10;
// 包含左右方括号，解析和消息分包使用同一个长度上限。
constexpr int MaximumTokenLength = 66;
const QVector<EmojiEntry> &Entries();
const EmojiEntry *FindById(const QString &id);
const EmojiEntry *FindByToken(const QString &token);
void InsertEmoji(QTextCursor &cursor, const EmojiEntry &emoji);
void InsertText(QTextCursor &cursor, const QString &text);
} // namespace EmojiCatalog

#endif
