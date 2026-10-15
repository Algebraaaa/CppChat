#include "contactnames.h"
#include <QFile>
#include <QHash>

QString ContactNames::initial(const QString &name)
{
  const auto trimmed = name.trimmed();
  if (trimmed.isEmpty())
    return QStringLiteral("#");
  const auto points = trimmed.toUcs4();
  const auto first = points.front();
  if (first < 128) {
    const QChar letter = QChar(static_cast<ushort>(first)).toUpper();
    return letter >= u'A' && letter <= u'Z' ? QString(letter) : QStringLiteral("#");
  }
  static const auto initials = [] {
    QHash<char32_t, QChar> result;
    QFile data(QStringLiteral(":/contacts/pinyin-initials.txt"));
    if (data.open(QIODevice::ReadOnly)) {
      while (!data.atEnd()) {
        const auto parts = data.readLine().trimmed().split(' ');
        if (parts.size() == 2)
          result.insert(parts[0].toUInt(nullptr, 16), QChar::fromLatin1(parts[1][0]));
      }
    }
    return result;
  }();
  return QString(initials.value(first, QChar(u'#')));
}
