#include "fileattachment.h"
#include <QCborMap>
#include <QCborValue>
#include <QFileInfo>
#include <QSaveFile>

bool FileAttachment::ValidName(const QString &name)
{
  if (name.isEmpty() || name == "." || name == ".." || name.toUtf8().size() > 255 ||
      name != name.trimmed() || name.endsWith('.') || QFileInfo(name).fileName() != name)
    return false;
  for (const auto ch : name)
    if (ch.isNull() || ch.category() == QChar::Other_Control ||
        ch.category() == QChar::Other_Format || QStringLiteral("/\\:*?\"<>|").contains(ch))
      return false;
  return true;
}

QByteArray FileAttachment::Pack(const QString &name, const QByteArray &bytes)
{
  if (!ValidName(name) || bytes.size() > MaximumPayloadBytes)
    return {};
  const QCborMap map{{QStringLiteral("v"), 1}, {QStringLiteral("name"), name},
                     {QStringLiteral("data"), bytes}};
  return QCborValue(map).toCbor();
}

FileAttachment::Document FileAttachment::Unpack(const QByteArray &encoded)
{
  if (encoded.isEmpty() || encoded.size() > MaximumBytes)
    return {};
  QCborParserError error;
  const auto value = QCborValue::fromCbor(encoded, &error);
  if (error.error != QCborError::NoError || error.offset != encoded.size() || !value.isMap())
    return {};
  const auto map = value.toMap();
  if (map.size() != 3 || !map[QStringLiteral("v")].isInteger() ||
      map[QStringLiteral("v")].toInteger() != 1 || !map[QStringLiteral("name")].isString() ||
      !map[QStringLiteral("data")].isByteArray())
    return {};
  const auto name = map[QStringLiteral("name")].toString();
  const auto bytes = map[QStringLiteral("data")].toByteArray();
  return ValidName(name) && bytes.size() <= MaximumPayloadBytes ? Document{name, bytes, true}
                                                              : Document{};
}

bool FileAttachment::Save(const Document &document, const QString &path, QString &error)
{
  if (!document.valid || path.isEmpty()) {
    error = QObject::tr("文件无效或尚未下载");
    return false;
  }
  QSaveFile output(path);
  // Android's user-selected content URI cannot be renamed; Qt uses its native
  // document provider only for that explicit destination.
  output.setDirectWriteFallback(path.startsWith("content://"));
  if (!output.open(QIODevice::WriteOnly) || output.write(document.bytes) != document.bytes.size() ||
      !output.commit()) {
    error = QObject::tr("保存文件失败：%1").arg(output.errorString());
    return false;
  }
  return true;
}
