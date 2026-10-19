#include "common/clientpreferences.h"
#include "thememanager.h"
#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QSettings>
#include <QStyleHints>
#include <QTimer>
#include <QWidget>

namespace {
bool systemDark(const QApplication &app)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  const auto scheme = app.styleHints()->colorScheme();
  if (scheme != Qt::ColorScheme::Unknown)
    return scheme == Qt::ColorScheme::Dark;
#endif
#ifdef Q_OS_WIN
  const QSettings settings(
      "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      QSettings::NativeFormat);
  return settings.value("AppsUseLightTheme", 1).toInt() == 0;
#else
  return app.palette().color(QPalette::Window).lightness() < 128;
#endif
}

// Colour-scheme signals precede the platform palette refresh. Coalesce them on the next
// event-loop turn, and ignore our own palette changes to avoid dark/light feedback loops.
class ThemeController final : public QObject {
public:
  explicit ThemeController(QApplication &app)
      : QObject(&app), _app(app), _systemDark(systemDark(app))
  {
    setObjectName("cppchat_theme_policy");
    _timer.setSingleShot(true);
    connect(&_timer, &QTimer::timeout, this, [this] {
      QString error;
      if (!Refresh(&error))
        qWarning() << "Appearance change failed:" << error;
    });
    connect(ClientPreferences::instance(), &ClientPreferences::changed, this,
            [this] { _timer.start(0); });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    _hasSchemeSignal = app.styleHints()->colorScheme() != Qt::ColorScheme::Unknown;
    connect(app.styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme scheme) {
              _hasSchemeSignal = scheme != Qt::ColorScheme::Unknown;
              _systemDark = scheme == Qt::ColorScheme::Unknown ? systemDark(_app)
                                                               : scheme == Qt::ColorScheme::Dark;
              _timer.start(0);
            });
#endif
    app.installEventFilter(this);
  }
  bool Refresh(QString *error)
  {
    const auto preference = ClientPreferences::instance()->Current().appearance;
    const bool dark = preference == ClientPreferences::Appearance::Dark ||
                      (preference == ClientPreferences::Appearance::System && _systemDark);
    if (_applied && _app.property("cppchat_theme_dark").toBool() == dark)
      return true;
    if (!ThemeManager::apply(_app, error,
                             dark ? ThemeManager::Variant::Dark : ThemeManager::Variant::Light))
      return false;
    _applied = true;
    return true;
  }

protected:
  bool eventFilter(QObject *object, QEvent *event) override
  {
    if (_app.property("cppchat_theme_applying").toBool())
      return false;
    const bool usePaletteFallback = !_hasSchemeSignal;
    const auto *widget = qobject_cast<QWidget *>(object);
    if (usePaletteFallback &&
        ((object == &_app && event->type() == QEvent::ApplicationPaletteChange) ||
         (event->type() == QEvent::ThemeChange &&
          (object == &_app || (widget && widget->isWindow()))))) {
      _systemDark = systemDark(_app);
      _timer.start(0);
    }
    return false;
  }

private:
  QApplication &_app;
  QTimer _timer;
  bool _systemDark, _applied = false;
  bool _hasSchemeSignal = false;
};
} // namespace

bool ThemeManager::followSystem(QApplication &app, QString *error)
{
  delete app.findChild<QObject *>("cppchat_theme_policy", Qt::FindDirectChildrenOnly);
  auto *controller = new ThemeController(app);
  if (controller->Refresh(error))
    return true;
  delete controller;
  return false;
}
