#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QString>
#include <QStringList>

class QApplication;

struct UiTheme {
  QString name;
  QString stylesheet;
  QFont font;
  QStringList fontResources;
  QColor windowBorderColor;
  QColor windowCaptionColor;
  QColor windowTextColor;
  QPalette palette;
  bool dark = false;
};

// 负责读取主题清单、展开参数和应用主题；业务页面只声明控件用途。
class ThemeManager {
public:
  enum class Variant { Light, Dark };
  // 加载成功后才替换输出；失败时保留 theme，error 返回具体原因。
  static bool load(const QString &manifestPath, UiTheme *theme, QString *error = nullptr,
                   Variant variant = Variant::Light);
  // One-shot deterministic application; useful for previews and isolated component tests.
  static bool apply(QApplication &application, QString *error = nullptr,
                    Variant variant = Variant::Light);
  // Production startup: observe the system and the saved System/Light/Dark preference.
  static bool followSystem(QApplication &application, QString *error = nullptr);
};

#endif // THEMEMANAGER_H
