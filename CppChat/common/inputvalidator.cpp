#include "common/inputvalidator.h"
#include <QRegularExpression>

namespace InputValidator {

QString userError(const QString &user)
{
  const QString normalizedUser = user.trimmed();
  static const QRegularExpression userRegex(R"(^[A-Za-z0-9_\x{4e00}-\x{9fff}]{3,20}$)");

  if (normalizedUser.isEmpty()) {
    return QStringLiteral("用户名不能为空");
  }
  if (!userRegex.match(normalizedUser).hasMatch()) {
    return QStringLiteral("用户名需为3到20位中文、字母、数字或下划线");
  }
  return {};
}

QString emailError(const QString &email)
{
  const QString normalizedEmail = email.trimmed();
  static const QRegularExpression emailRegex(R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)");

  if (normalizedEmail.isEmpty()) {
    return QStringLiteral("邮箱不能为空");
  }
  if (!emailRegex.match(normalizedEmail).hasMatch()) {
    return QStringLiteral("邮箱格式不正确");
  }
  return {};
}

QString passwordError(const QString &password)
{
  static const QRegularExpression whitespaceRegex(R"(\s)");
  static const QRegularExpression letterRegex(R"([A-Za-z])");
  static const QRegularExpression digitRegex(R"(\d)");

  if (password.isEmpty()) {
    return QStringLiteral("密码不能为空");
  }
  if (password.size() < 8 || password.size() > 32) {
    return QStringLiteral("密码长度需为8到32位");
  }
  if (password.contains(whitespaceRegex) || !password.contains(letterRegex)
      || !password.contains(digitRegex)) {
    return QStringLiteral("密码必须包含字母和数字，且不能包含空白字符");
  }
  return {};
}

QString confirmPasswordError(const QString &password, const QString &confirmPassword)
{
  if (confirmPassword.isEmpty()) {
    return QStringLiteral("确认密码不能为空");
  }
  if (confirmPassword != password) {
    return QStringLiteral("密码和确认密码不匹配");
  }
  return {};
}

QString verifyCodeError(const QString &verifyCode)
{
  const QString normalizedCode = verifyCode.trimmed();
  static const QRegularExpression verifyCodeRegex(R"(^\d{6}$)");

  if (normalizedCode.isEmpty()) {
    return QStringLiteral("验证码不能为空");
  }
  if (!verifyCodeRegex.match(normalizedCode).hasMatch()) {
    return QStringLiteral("验证码应为6位数字");
  }
  return {};
}

} // namespace InputValidator
