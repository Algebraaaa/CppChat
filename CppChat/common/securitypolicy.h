#pragma once
#include <QString>
#include <QUrl>

namespace SecurityPolicy {
bool requiresTls();
bool transportAllowed(const QUrl &url);
bool socketAllowed(bool tls, const QString &host = {});
} // namespace SecurityPolicy
