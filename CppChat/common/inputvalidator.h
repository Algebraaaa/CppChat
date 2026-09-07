#ifndef INPUTVALIDATOR_H
#define INPUTVALIDATOR_H

#include <QString>

// 这里只保存与界面无关的输入规则。
// 返回空字符串表示校验通过，否则返回可直接展示给用户的错误提示。
namespace InputValidator {
QString userError(const QString &user);
QString emailError(const QString &email);
QString passwordError(const QString &password);
QString confirmPasswordError(const QString &password, const QString &confirmPassword);
QString verifyCodeError(const QString &verifyCode);
} // namespace InputValidator

#endif // INPUTVALIDATOR_H
