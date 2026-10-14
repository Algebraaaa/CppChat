#include "app/securitycommands.h"
#include "common/keyrecovery.h"
#include "common/localdataprotection.h"
#include "common/localstoresecurity.h"
#include "common/logger.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace SecurityCommands {
void registerArguments(QCommandLineParser &arguments)
{
  arguments.addOptions(
      {{"secure-local-data", "Protect and migrate an existing chat_data directory", "directory"},
       {"restore-local-backup", "Restore a verified encrypted account backup", "directory"},
       {"restore-target", "Empty destination for backup restore", "directory"},
       {"secure-installation", "Protect the application installation and existing chat_data"},
       {"export-recovery-keys", "Export password-protected local recovery keys", "file"},
       {"import-recovery-keys", "Import local recovery keys after copying encrypted data",
        "file"}});
}
std::optional<int> execute(QApplication &a, const QCommandLineParser &arguments)
{
  if (arguments.isSet("secure-local-data") || arguments.isSet("restore-local-backup") ||
      arguments.isSet("secure-installation") || arguments.isSet("export-recovery-keys") ||
      arguments.isSet("import-recovery-keys")) {
    Logger::install();
    try {
      if (arguments.isSet("secure-installation")) {
        LocalDataProtection::secureDirectory(a.applicationDirPath(), true);
        const QString root = a.applicationDirPath() + "/chat_data";
        if (QDir(root).exists())
          LocalStoreSecurity::prepareRoot(root);
        std::cout << "Installation permissions and existing chat data protected" << std::endl;
      } else if (arguments.isSet("export-recovery-keys") ||
                 arguments.isSet("import-recovery-keys")) {
        const bool exporting = arguments.isSet("export-recovery-keys");
        bool accepted = false;
        const QString password = QInputDialog::getText(
            nullptr, QObject::tr("聊天数据恢复密钥"),
            QObject::tr("请输入至少 12 个字符的恢复密码。请妥善保存，忘记后无法恢复。"),
            QLineEdit::Password, {}, &accepted);
        if (!accepted)
          return EXIT_SUCCESS;
        const QString root = arguments.value("data-dir").isEmpty()
                                 ? a.applicationDirPath() + "/chat_data"
                                 : arguments.value("data-dir");
        if (exporting) {
          const QString confirmation = QInputDialog::getText(nullptr, QObject::tr("确认恢复密码"),
                                                             QObject::tr("再次输入恢复密码"),
                                                             QLineEdit::Password, {}, &accepted);
          if (!accepted)
            return EXIT_SUCCESS;
          if (password != confirmation)
            throw std::runtime_error("Recovery passwords differ");
          const QString target = arguments.value("export-recovery-keys");
          if (QFile::exists(target))
            throw std::runtime_error("Recovery export refuses to overwrite an existing file");
          LocalDataProtection::atomicWrite(target, KeyRecovery::exportKeys(root, password));
        } else {
          QFile recovery(arguments.value("import-recovery-keys"));
          if (!recovery.open(QIODevice::ReadOnly) || recovery.size() > 1024 * 1024)
            throw std::runtime_error("Invalid recovery file");
          KeyRecovery::importKeys(root, recovery.readAll(), password);
        }
        QMessageBox::information(
            nullptr, QObject::tr("恢复密钥"),
            exporting
                ? QObject::tr(
                      "恢复密钥已导出。换电脑时需同时备份 chat_data 和此文件，并记住恢复密码。")
                : QObject::tr("恢复密钥已导入。"));
      } else if (arguments.isSet("secure-local-data")) {
        const int count = LocalStoreSecurity::prepareRoot(arguments.value("secure-local-data"));
        std::cout << "Protected accounts: " << count << std::endl;
      } else {
        if (!arguments.isSet("restore-target"))
          throw std::runtime_error("--restore-target is required");
        LocalStoreSecurity::restoreBackup(arguments.value("restore-local-backup"),
                                          arguments.value("restore-target"));
        std::cout << "Encrypted backup restored and verified" << std::endl;
      }
      return EXIT_SUCCESS;
    } catch (const std::exception &error) {
      qCritical() << "Local security operation failed:" << error.what();
      if (arguments.isSet("export-recovery-keys") || arguments.isSet("import-recovery-keys"))
        QMessageBox::critical(nullptr, QObject::tr("恢复密钥操作失败"),
                              QString::fromUtf8(error.what()));
      std::cerr << error.what() << std::endl;
      return EXIT_FAILURE;
    }
  }

  return std::nullopt;
}
} // namespace SecurityCommands
