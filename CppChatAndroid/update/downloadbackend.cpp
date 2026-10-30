#include "downloadbackend.h"
#include <QJsonDocument>
#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>

namespace {
class AndroidDownloadBackend final : public MobileDownloadBackend
{
public:
  AndroidDownloadBackend()
  {
    // Qt 6.12 returns a typed Context; use its documented QJniObject conversion.
    const QJniObject nativeContext = QNativeInterface::QAndroidApplication::context();
    context =
        nativeContext.callObjectMethod("getApplicationContext", "()Landroid/content/Context;");
  }
  QJsonObject inspect() override
  {
    return call("inspect");
  }
  QJsonObject cancel() override
  {
    return call("cancel");
  }
  QJsonObject verify() override
  {
    return call("verify");
  }
  QJsonObject install() override
  {
    return call("install");
  }
  QJsonObject start(const QJsonObject &manifest) override
  {
    return call("start", manifest);
  }

private:
  QJsonObject call(const char *method, const QJsonObject &manifest = {})
  {
    constexpr auto type = "org/algebra/cppchat/UpdateDownload";
    QJniObject result;
    if (manifest.isEmpty()) {
      result = QJniObject::callStaticObjectMethod(
          type, method, "(Landroid/content/Context;)Ljava/lang/String;", context.object<jobject>());
    } else {
      const auto value = QJniObject::fromString(
          QString::fromUtf8(QJsonDocument(manifest).toJson(QJsonDocument::Compact)));
      result = QJniObject::callStaticObjectMethod(
          type, method, "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
          context.object<jobject>(), value.object<jstring>());
    }
    QJniEnvironment env;
    if (env.checkAndClearExceptions() || !result.isValid())
      return {{"error", "安卓系统下载服务调用失败"}};
    const auto object = QJsonDocument::fromJson(result.toString().toUtf8()).object();
    return object.isEmpty() ? QJsonObject{{"error", "安卓系统下载服务响应无效"}} : object;
  }
  QJniObject context;
};
} // namespace
#endif

std::shared_ptr<MobileDownloadBackend> makeAndroidDownloadBackend()
{
#ifdef Q_OS_ANDROID
  return std::make_shared<AndroidDownloadBackend>();
#else
  return {};
#endif
}
