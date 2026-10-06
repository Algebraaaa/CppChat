#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QFont>
#include <QColor>
#include <QString>
#include <QStringList>

class QApplication;

struct UiTheme
{
  QString name;
  QString stylesheet;
  QFont font;
  QStringList fontResources;
  QColor windowBorderColor;
  QColor windowCaptionColor;
  QColor windowTextColor;
};

// 负责读取主题清单、展开参数和应用主题；业务页面只声明控件用途。
class ThemeManager
{
public:
  // 加载成功后才替换输出；失败时保留 theme，error 返回具体原因。
  static bool load(const QString &manifestPath, UiTheme *theme, QString *error = nullptr);
  // 启动时在创建页面前调用；当前主题来自内置 Qt 资源。
  static bool apply(QApplication &application, QString *error = nullptr);
};

#endif // THEMEMANAGER_H
