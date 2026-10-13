#include "thememanager.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QWidget>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <qt_windows.h>
#endif

namespace {
#ifdef Q_OS_WIN
// QSS 只能绘制客户区，Windows 的外框与原生标题栏需要通过 DWM 单独设置。
// 统一监听顶层窗口，聊天记录、个人资料、图片预览和 Qt 提示框无需逐个复制代码。
class WindowFrameFilter final : public QObject {
public:
  WindowFrameFilter(QApplication &application, const UiTheme &theme)
      : QObject(&application), _border(theme.windowBorderColor), _caption(theme.windowCaptionColor),
        _text(theme.windowTextColor)
  {
    setObjectName(QStringLiteral("cppchat_window_frame_filter"));
    application.installEventFilter(this);
    for (auto *widget : QApplication::topLevelWidgets())
      ApplyFrame(widget);
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override
  {
    switch (event->type()) {
    case QEvent::Show:
    case QEvent::WinIdChange:
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate:
    case QEvent::ThemeChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::PaletteChange:
      break;
    default:
      return QObject::eventFilter(watched, event);
    }
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || !widget->isWindow())
      return QObject::eventFilter(watched, event);
    ApplyFrame(widget);
    // Show/WinIdChange 可能早于平台窗口初始化完成；事件处理完后再补一次。
    // QPointer 防止临时消息框在排队期间被销毁，过滤器不吞掉窗口事件。
    if (!widget->property("cppchat_frame_update_pending").toBool()) {
      widget->setProperty("cppchat_frame_update_pending", true);
      const QPointer<QWidget> window = widget;
      QTimer::singleShot(0, this, [this, window] {
        if (window) {
          window->setProperty("cppchat_frame_update_pending", false);
          ApplyFrame(window.data());
        }
      });
    }
    return QObject::eventFilter(watched, event);
  }

private:
  void ApplyFrame(QWidget *widget) const
  {
    if (!widget || !widget->isWindow() || widget->isFullScreen() ||
        widget->windowType() == Qt::Popup || widget->windowType() == Qt::ToolTip ||
        widget->testAttribute(Qt::WA_TranslucentBackground))
      return; // 透明圆角弹窗/菜单沿用 QSS 自绘边框，避免额外加一层系统外框。
    // 只读取现有句柄；不要调用 winId() 强制给子控件创建原生窗口。
    const HWND hwnd = reinterpret_cast<HWND>(widget->internalWinId());
    if (!hwnd)
      return;
    const COLORREF border = RGB(_border.red(), _border.green(), _border.blue());
    const COLORREF caption = RGB(_caption.red(), _caption.green(), _caption.blue());
    const COLORREF text = RGB(_text.red(), _text.green(), _text.blue());
    // 数值兼容 Qt 6.5 所用的旧 MinGW 头文件：34/35/36 分别为 BORDER/CAPTION/TEXT_COLOR。
    // Windows 11 支持这些属性；旧系统不支持时返回失败，安全保留系统原有外观。
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36, &text, sizeof(text));
  }

  const QColor _border;
  const QColor _caption;
  const QColor _text;
};
#endif

bool fail(QString *error, const QString &message)
{
  if (error) {
    *error = message;
  }
  return false;
}

bool readObject(const QString &path, QJsonObject *object, QString *error)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return fail(error, QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    return fail(error,
                QStringLiteral("Invalid theme JSON in %1: %2").arg(path, parseError.errorString()));
  }
  if (!document.isObject()) {
    return fail(error, QStringLiteral("Theme JSON root must be an object: %1").arg(path));
  }
  *object = document.object();
  return true;
}

bool readStringList(const QJsonObject &object, const QString &key, QStringList *strings,
                    QString *error)
{
  const auto value = object.value(key);
  if (!value.isArray() || value.toArray().isEmpty()) {
    return fail(error, QStringLiteral("Theme field %1 must be a nonempty array").arg(key));
  }
  QSet<QString> seen;
  for (const auto &entry : value.toArray()) {
    if (!entry.isString() || entry.toString().trimmed().isEmpty()) {
      return fail(error, QStringLiteral("Theme field %1 contains an invalid entry").arg(key));
    }
    const QString text = entry.toString();
    if (seen.contains(text)) {
      return fail(error, QStringLiteral("Duplicate %1 entry: %2").arg(key, text));
    }
    seen.insert(text);
    strings->append(text);
  }
  return true;
}

bool expandTokens(const QString &source, const QJsonObject &tokens, const QString &path,
                  QString *expanded, QString *error)
{
  static const QRegularExpression pattern(QStringLiteral(R"(\$\{([a-zA-Z][a-zA-Z0-9_.-]*)\})"));
  auto matches = pattern.globalMatch(source);
  QString result;
  qsizetype offset = 0;
  while (matches.hasNext()) {
    const auto match = matches.next();
    const QString key = match.captured(1);
    if (!tokens.contains(key)) {
      return fail(error, QStringLiteral("Undefined theme token %1 in %2").arg(key, path));
    }
    result += source.mid(offset, match.capturedStart() - offset);
    result += tokens.value(key).toString();
    offset = match.capturedEnd();
  }
  result += source.mid(offset);
  if (result.contains(QStringLiteral("${"))) {
    return fail(error, QStringLiteral("Invalid token expression in %1").arg(path));
  }
  *expanded = result;
  return true;
}
} // namespace

bool ThemeManager::load(const QString &manifestPath, UiTheme *theme, QString *error)
{
  if (error) {
    error->clear();
  }
  if (!theme) {
    return fail(error, QStringLiteral("Theme output must not be null"));
  }

  QJsonObject manifest;
  if (!readObject(manifestPath, &manifest, error)) {
    return false;
  }
  const QDir directory(QFileInfo(manifestPath).path());
  const QString tokenFile = manifest.value(QStringLiteral("tokens")).toString();
  if (tokenFile.trimmed().isEmpty()) {
    return fail(error, QStringLiteral("Theme tokens path is missing: %1").arg(manifestPath));
  }

  QJsonObject tokens;
  if (!readObject(directory.filePath(tokenFile), &tokens, error)) {
    return false;
  }
  for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
    if (!it.value().isString() || it.value().toString().isEmpty() ||
        it.value().toString().contains(QStringLiteral("${"))) {
      return fail(error, QStringLiteral("Invalid theme token value: %1").arg(it.key()));
    }
  }
  if (tokens.contains(QStringLiteral("font.family"))) {
    return fail(error, QStringLiteral("font.family is derived from fontFamilies"));
  }

  UiTheme loaded;
  loaded.windowBorderColor = QColor(tokens.value(QStringLiteral("color.border.window")).toString());
  loaded.windowCaptionColor =
      QColor(tokens.value(QStringLiteral("color.surface.subtle")).toString());
  loaded.windowTextColor = QColor(tokens.value(QStringLiteral("color.text.primary")).toString());
  if (!loaded.windowBorderColor.isValid() || !loaded.windowCaptionColor.isValid() ||
      !loaded.windowTextColor.isValid()) {
    return fail(
        error,
        QStringLiteral("Invalid window frame color in %1").arg(directory.filePath(tokenFile)));
  }
  loaded.name = manifest.value(QStringLiteral("name")).toString();
  if (loaded.name.trimmed().isEmpty()) {
    return fail(error, QStringLiteral("Theme name is missing: %1").arg(manifestPath));
  }
  QStringList families;
  QStringList modules;
  if (!readStringList(manifest, QStringLiteral("fontFamilies"), &families, error) ||
      !readStringList(manifest, QStringLiteral("fontResources"), &loaded.fontResources, error) ||
      !readStringList(manifest, QStringLiteral("styles"), &modules, error)) {
    return false;
  }

  QStringList quotedFamilies;
  for (const auto &family : families) {
    if (family.contains(QLatin1Char('"')) || family.contains(QLatin1Char('\\'))) {
      return fail(error, QStringLiteral("Invalid font family: %1").arg(family));
    }
    quotedFamilies.append(QLatin1Char('"') + family + QLatin1Char('"'));
  }
  tokens.insert(QStringLiteral("font.family"), quotedFamilies.join(QStringLiteral(", ")));
  const QString baseSize = tokens.value(QStringLiteral("font.size.base")).toString();
  static const QRegularExpression pixelSizePattern(QStringLiteral(R"(^([1-9][0-9]*)px$)"));
  const auto sizeMatch = pixelSizePattern.match(baseSize);
  bool sizeOk = false;
  const int pixels = sizeMatch.captured(1).toInt(&sizeOk);
  if (!sizeMatch.hasMatch() || !sizeOk) {
    return fail(error, QStringLiteral("Invalid font.size.base in %1: %2")
                           .arg(directory.filePath(tokenFile), baseSize));
  }
  loaded.font.setFamilies(families);
  loaded.font.setPixelSize(pixels);
  loaded.font.setWeight(QFont::Normal);
  loaded.font.setStyleHint(QFont::SansSerif);

  // 清单顺序就是覆盖顺序；全部读取成功后才向调用者返回主题。
  for (const auto &module : modules) {
    const QString path = directory.filePath(module);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      return fail(error,
                  QStringLiteral("Cannot read style module %1: %2").arg(path, file.errorString()));
    }
    QString expanded;
    if (!expandTokens(QString::fromUtf8(file.readAll()), tokens, path, &expanded, error)) {
      return false;
    }
    loaded.stylesheet +=
        QStringLiteral("/* Module: %1 */\n").arg(module) + expanded + QLatin1Char('\n');
  }
  for (const auto &font : loaded.fontResources) {
    if (!QFile::exists(font)) {
      return fail(error, QStringLiteral("Missing font resource: %1").arg(font));
    }
  }
  *theme = loaded;
  return true;
}

bool ThemeManager::apply(QApplication &application, QString *error)
{
  UiTheme theme;
  if (!load(QStringLiteral(":/style/theme.json"), &theme, error)) {
    return false;
  }
  QList<int> registeredFonts;
  for (const auto &path : theme.fontResources) {
    const int id = QFontDatabase::addApplicationFont(path);
    if (id < 0) {
      for (int registered : registeredFonts) {
        QFontDatabase::removeApplicationFont(registered);
      }
      return fail(error, QStringLiteral("Cannot register font resource: %1").arg(path));
    }
    registeredFonts.append(id);
  }
  application.setFont(theme.font);
  application.setStyleSheet(theme.stylesheet);
#ifdef Q_OS_WIN
  // 重复应用主题时替换旧过滤器；生命周期交给 QApplication 管理。
  delete application.findChild<QObject *>(QStringLiteral("cppchat_window_frame_filter"),
                                          Qt::FindDirectChildrenOnly);
  new WindowFrameFilter(application, theme);
#endif
  return true;
}
