#pragma once
#include <QByteArray>
#include <QString>

// A standard CBOR document keeps the original name and arbitrary bytes together.
// ResourceMgr provides transport integrity, authorization and account-local storage.
namespace FileAttachment {
inline constexpr int MaximumBytes = 20 * 1024 * 1024;
inline constexpr int MaximumPayloadBytes = MaximumBytes - 1024;
struct Document {
  QString name;
  QByteArray bytes;
  bool valid = false;
};
bool ValidName(const QString &name);
QByteArray Pack(const QString &name, const QByteArray &bytes);
Document Unpack(const QByteArray &encoded);
bool Save(const Document &document, const QString &path, QString &error);
} // namespace FileAttachment
