#include "common/localdataprotection.h"
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QScopeGuard>
#include <stdexcept>

#ifdef Q_OS_WIN
#include <qt_windows.h>

#include <aclapi.h>
#include <bcrypt.h>
#include <dpapi.h>
#include <sddl.h>
#endif

namespace {
const QByteArray kMagic("CPPCHAT-DPAPI-1\0", 16);
const QByteArray kPayloadMagic("CPPCHAT-AESGCM-1\0", 16);
void require(bool ok, const QString &message)
{
  if (!ok)
    throw std::runtime_error(message.toStdString());
}
#ifdef Q_OS_WIN
QString currentSid()
{
  HANDLE token = nullptr;
  require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token), "Cannot query user token");
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  QByteArray information(static_cast<qsizetype>(size), 0);
  const bool read = GetTokenInformation(token, TokenUser, information.data(), size, &size);
  CloseHandle(token);
  require(read, "Cannot query current Windows user");
  LPWSTR sid = nullptr;
  require(
      ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(information.data())->User.Sid, &sid),
      "Cannot resolve current Windows user");
  const QString value = QString::fromWCharArray(sid);
  LocalFree(sid);
  return value;
}
void restrictPath(const QString &path)
{
  const auto native = QDir::toNativeSeparators(path).toStdWString();
  const DWORD attributes = GetFileAttributesW(native.c_str());
  require(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
          QStringLiteral("数据路径不存在或包含重解析链接：%1").arg(path));
  const auto descriptorText =
      ("D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" + currentSid() + ")").toStdWString();
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  require(ConvertStringSecurityDescriptorToSecurityDescriptorW(
              descriptorText.c_str(), SDDL_REVISION_1, &descriptor, nullptr),
          "Cannot create private file permissions");
  PACL acl = nullptr;
  BOOL present = FALSE, defaulted = FALSE;
  const bool valid = GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted);
  DWORD result = ERROR_INVALID_SECURITY_DESCR;
  if (valid && present)
    result = SetNamedSecurityInfoW(const_cast<LPWSTR>(native.c_str()), SE_FILE_OBJECT,
                                   DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                   nullptr, nullptr, acl, nullptr);
  LocalFree(descriptor);
  require(result == ERROR_SUCCESS,
          QStringLiteral("无法保护数据路径（Windows 错误 %1）：%2").arg(result).arg(path));
}
QByteArray aesGcm(const QByteArray &input, const QByteArray &key, const QByteArray &nonce,
                  QByteArray &tag, bool encrypt)
{
  require(key.size() == 32 && nonce.size() == 12 && tag.size() == 16 &&
              input.size() <= 32 * 1024 * 1024,
          "Invalid AES-GCM parameters");
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) == 0,
          "AES provider unavailable");
  auto closeAlgorithm = qScopeGuard([&] { BCryptCloseAlgorithmProvider(algorithm, 0); });
  require(BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                            reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
                            sizeof(BCRYPT_CHAIN_MODE_GCM), 0) == 0,
          "AES-GCM unavailable");
  DWORD objectSize = 0, received = 0;
  require(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
                            sizeof(objectSize), &received, 0) == 0,
          "Cannot query AES key size");
  QByteArray object(objectSize, 0);
  BCRYPT_KEY_HANDLE handle = nullptr;
  require(BCryptGenerateSymmetricKey(
              algorithm, &handle, reinterpret_cast<PUCHAR>(object.data()), object.size(),
              reinterpret_cast<PUCHAR>(const_cast<char *>(key.constData())), key.size(), 0) == 0,
          "Cannot initialize AES key");
  auto closeKey = qScopeGuard([&] {
    BCryptDestroyKey(handle);
    SecureZeroMemory(object.data(), object.size());
  });
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;
  BCRYPT_INIT_AUTH_MODE_INFO(auth);
  auth.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char *>(nonce.constData()));
  auth.cbNonce = nonce.size();
  auth.pbTag = reinterpret_cast<PUCHAR>(tag.data());
  auth.cbTag = tag.size();
  auth.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<char *>(kPayloadMagic.constData()));
  auth.cbAuthData = kPayloadMagic.size();
  QByteArray output(input.size(), 0);
  ULONG written = 0;
  const auto operation = encrypt ? BCryptEncrypt : BCryptDecrypt;
  require(operation(handle, reinterpret_cast<PUCHAR>(const_cast<char *>(input.constData())),
                    input.size(), &auth, nullptr, 0, reinterpret_cast<PUCHAR>(output.data()),
                    output.size(), &written, 0) == 0 &&
              written == input.size(),
          "Encrypted payload authentication failed");
  return output;
}
#endif
} // namespace

namespace LocalDataProtection {
void secureDirectory(const QString &directory, bool recursive)
{
  const QString absolute = QFileInfo(directory).absoluteFilePath();
  require(!QDir(absolute).isRoot(), "Refusing to change permissions on a drive root");
  require(QDir().mkpath(absolute), QStringLiteral("无法创建私有目录：%1").arg(absolute));
#ifdef Q_OS_WIN
  restrictPath(absolute);
#else
  require(QFile::setPermissions(absolute, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
          "Cannot restrict data directory permissions");
#endif
  if (!recursive)
    return;
  const auto entries = QDir(absolute).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot |
                                                    QDir::Hidden | QDir::System);
  for (const auto &entry : entries) {
    require(!entry.isSymLink(), "Refusing to change permissions through a link");
    if (entry.isDir())
      secureDirectory(entry.filePath(), true);
    else {
#ifdef Q_OS_WIN
      restrictPath(entry.filePath());
#else
      require(QFile::setPermissions(entry.filePath(), QFile::ReadOwner | QFile::WriteOwner),
              "Cannot restrict private data file permissions");
#endif
    }
  }
}

bool isProtected(const QByteArray &bytes) { return bytes.startsWith(kMagic); }
bool isEncryptedPayload(const QByteArray &bytes) { return bytes.startsWith(kPayloadMagic); }
QByteArray encryptPayload(const QByteArray &plain, const QByteArray &key)
{
#ifdef Q_OS_WIN
  const auto nonce = randomBytes(12);
  QByteArray tag(16, 0);
  const auto cipher = aesGcm(plain, key, nonce, tag, true);
  return kPayloadMagic + nonce + tag + cipher;
#else
  Q_UNUSED(key);
  return plain;
#endif
}
QByteArray decryptPayload(const QByteArray &encrypted, const QByteArray &key)
{
#ifdef Q_OS_WIN
  require(isEncryptedPayload(encrypted) && encrypted.size() >= 44 &&
              encrypted.size() <= 33 * 1024 * 1024,
          "Invalid encrypted attachment envelope");
  QByteArray tag = encrypted.mid(28, 16);
  return aesGcm(encrypted.mid(44), key, encrypted.mid(16, 12), tag, false);
#else
  Q_UNUSED(key);
  return encrypted;
#endif
}

QByteArray protect(const QByteArray &plain, const QByteArray &context)
{
#ifdef Q_OS_WIN
  require(plain.size() <= 32 * 1024 * 1024, "Private payload exceeds encryption budget");
  DATA_BLOB input{static_cast<DWORD>(plain.size()),
                  reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
  DATA_BLOB entropy{static_cast<DWORD>(context.size()),
                    reinterpret_cast<BYTE *>(const_cast<char *>(context.constData()))};
  DATA_BLOB output{};
  require(CryptProtectData(&input, L"CppChat private data", &entropy, nullptr, nullptr,
                           CRYPTPROTECT_UI_FORBIDDEN, &output),
          "Windows cannot encrypt private data");
  const QByteArray result =
      kMagic + QByteArray(reinterpret_cast<char *>(output.pbData), output.cbData);
  SecureZeroMemory(output.pbData, output.cbData);
  LocalFree(output.pbData);
  return result;
#else
  Q_UNUSED(context);
  return plain;
#endif
}

QByteArray unprotect(const QByteArray &encrypted, const QByteArray &context)
{
#ifdef Q_OS_WIN
  require(isProtected(encrypted) && encrypted.size() <= 33 * 1024 * 1024,
          "Private data is not a valid encrypted payload");
  DATA_BLOB input{
      static_cast<DWORD>(encrypted.size() - kMagic.size()),
      reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.constData() + kMagic.size()))};
  DATA_BLOB entropy{static_cast<DWORD>(context.size()),
                    reinterpret_cast<BYTE *>(const_cast<char *>(context.constData()))};
  DATA_BLOB output{};
  require(CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                             &output),
          "无法解密本地数据：Windows 用户、密钥或文件不匹配。请恢复备份或导入恢复密钥。");
  const QByteArray result(reinterpret_cast<char *>(output.pbData), output.cbData);
  SecureZeroMemory(output.pbData, output.cbData);
  LocalFree(output.pbData);
  return result;
#else
  Q_UNUSED(context);
  return encrypted;
#endif
}

QByteArray randomBytes(int size)
{
  require(size > 0 && size <= 64, "Invalid random byte request");
  QByteArray key(size, 0);
#ifdef Q_OS_WIN
  require(BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(key.data()), size,
                          BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0,
          "Windows cannot generate encryption key");
#endif
  return key;
}
QByteArray randomKey() { return randomBytes(32); }
QByteArray passwordKey(const QString &password, const QByteArray &salt)
{
  require(password.size() >= 12 && salt.size() == 16, "恢复密码至少 12 个字符。");
#ifdef Q_OS_WIN
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                      BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0,
          "Password KDF unavailable");
  auto cleanup = qScopeGuard([&] { BCryptCloseAlgorithmProvider(algorithm, 0); });
  const QByteArray bytes = password.toUtf8();
  QByteArray key(32, 0);
  require(BCryptDeriveKeyPBKDF2(
              algorithm, reinterpret_cast<PUCHAR>(const_cast<char *>(bytes.constData())),
              bytes.size(), reinterpret_cast<PUCHAR>(const_cast<char *>(salt.constData())),
              salt.size(), 300000, reinterpret_cast<PUCHAR>(key.data()), key.size(), 0) == 0,
          "Cannot derive recovery key");
  return key;
#else
  throw std::runtime_error("Recovery encryption is currently supported on Windows");
#endif
}

void atomicWrite(const QString &path, const QByteArray &bytes)
{
  QSaveFile file(path);
  require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
          QStringLiteral("无法提交私有文件：%1").arg(path));
}
} // namespace LocalDataProtection
