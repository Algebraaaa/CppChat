#ifndef FORMFEEDBACK_H
#define FORMFEEDBACK_H

#include <QList>
#include <QObject>
#include <QString>
#include <functional>

class QLabel;
class QLineEdit;

// 复用 Telegram 的输入变化/提交校验时机和 Element Plus 的字段状态模型。
// 参考源码和本项目的适配规则见 docs/ui-features.md 的“表单校验”。
class FormFeedback : public QObject {
public:
  using Validator = std::function<QString()>;

  explicit FormFeedback(QLabel *label, QObject *parent);
  void addField(QLineEdit *input, Validator validator, const QList<QLineEdit *> &dependencies = {});
  bool validateField(QLineEdit *input, bool focusOnError = false);
  bool validateAll();
  void showStatus(const QString &message, bool ok, const QList<QLineEdit *> &relatedInputs = {});
  void clearStatus();
  void reset();

private:
  struct Field {
    QLineEdit *input;
    Validator validator;
    QList<QLineEdit *> dependencies;
    bool checked = false;
    QString error;
  };

  void inputChanged(QLineEdit *input);
  void updateField(Field &field);
  void refresh();

  QLabel *_label;
  QList<Field> _fields; // 登记顺序就是界面顺序，不依赖枚举值排序。
  QString _statusMessage;
  bool _statusOk = true;
  QList<QLineEdit *> _statusInputs;
};

#endif // FORMFEEDBACK_H
