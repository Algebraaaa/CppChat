#pragma once
#include <QByteArray>
#include <QString>

namespace LocalDataProtection {
// Restrict owned application data; never pass a drive root or arbitrary parent directory.
void secureDirectory(const QString &directory, bool recursive = false);
QByteArray protect(const QByteArray &plain, const QByteArray &context);
QByteArray unprotect(const QByteArray &encrypted, const QByteArray &context);
bool isProtected(const QByteArray &bytes);
bool isEncryptedPayload(const QByteArray &bytes);
QByteArray encryptPayload(const QByteArray &plain, const QByteArray &key);
QByteArray decryptPayload(const QByteArray &encrypted, const QByteArray &key);
QByteArray randomBytes(int size);
QByteArray passwordKey(const QString &password, const QByteArray &salt);
QByteArray randomKey();
void atomicWrite(const QString &path, const QByteArray &bytes);
} // namespace LocalDataProtection
