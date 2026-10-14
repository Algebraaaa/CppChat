#include "common/securitypolicy.h"
#include <QHostAddress>

namespace SecurityPolicy {
bool requiresTls()
{
#ifdef CPPCHAT_DEVELOPMENT_TRANSPORT
  return false;
#else
  return true;
#endif
}
bool transportAllowed(const QUrl &url)
{
  const bool local = url.host().compare("localhost", Qt::CaseInsensitive) == 0 ||
                     QHostAddress(url.host()).isLoopback();
  return url.isValid() && !url.host().isEmpty() &&
         (url.scheme() == QStringLiteral("https") ||
          (!requiresTls() && local && url.scheme() == QStringLiteral("http")));
}
bool socketAllowed(bool tls, const QString &host)
{
  return tls || (!requiresTls() && (host.compare("localhost", Qt::CaseInsensitive) == 0 ||
                                    QHostAddress(host).isLoopback()));
}
} // namespace SecurityPolicy
