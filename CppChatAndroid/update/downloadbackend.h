#pragma once

#include <QJsonObject>
#include <memory>

// Platform transfer ownership stays outside the Qt UI/process lifecycle.
class MobileDownloadBackend
{
public:
  virtual ~MobileDownloadBackend() = default;
  virtual QJsonObject inspect() = 0;
  virtual QJsonObject start(const QJsonObject &manifest) = 0;
  virtual QJsonObject cancel() = 0;
  virtual QJsonObject verify() = 0;
  virtual QJsonObject install() = 0;
};

std::shared_ptr<MobileDownloadBackend> makeAndroidDownloadBackend();
