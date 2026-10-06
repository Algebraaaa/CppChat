#include "common/formfeedback.h"
#include "common/global.h"
#include <QLabel>
#include <QLineEdit>
#include <utility>

FormFeedback::FormFeedback(QLabel *label, QObject *parent) : QObject(parent), _label(label)
{
  refresh();
}

void FormFeedback::addField(QLineEdit *input, Validator validator,
                           const QList<QLineEdit *> &dependencies)
{
  _fields.append({input, std::move(validator), dependencies, false, {}});
  connect(input, &QLineEdit::textChanged, this, [this, input]() { inputChanged(input); });
  connect(input, &QLineEdit::editingFinished, this,
          [this, input]() { validateField(input); });
}

void FormFeedback::updateField(Field &field)
{
  field.error = field.validator();
  field.input->setProperty("validationError", !field.error.isEmpty());
  repolish(field.input);
}

void FormFeedback::inputChanged(QLineEdit *input)
{
  // 请求提示只在关联输入变化时失效，例如修改密码不会清掉验证码发送结果。
  if (_statusInputs.contains(input)) {
    _statusMessage.clear();
    _statusInputs.clear();
  }
  for (Field &field : _fields) {
    const bool dependent = field.dependencies.contains(input);
    if ((field.input == input && field.checked) ||
        (dependent && (field.checked || !field.input->text().isEmpty()))) {
      field.checked = true;
      updateField(field);
    }
  }
  refresh();
}

bool FormFeedback::validateField(QLineEdit *input, bool focusOnError)
{
  bool valid = true;
  for (Field &field : _fields) {
    if (field.input == input) {
      field.checked = true;
      updateField(field);
      valid = field.error.isEmpty();
    } else if (field.dependencies.contains(input) &&
               (field.checked || !field.input->text().isEmpty())) {
      field.checked = true;
      updateField(field);
    }
  }
  refresh();
  if (!valid && focusOnError) {
    input->setFocus();
  }
  return valid;
}

bool FormFeedback::validateAll()
{
  QLineEdit *firstInvalid = nullptr;
  // 所有字段都更新，避免短路返回后留下上一次提交的错误。
  for (Field &field : _fields) {
    field.checked = true;
    updateField(field);
    if (!firstInvalid && !field.error.isEmpty()) {
      firstInvalid = field.input;
    }
  }
  refresh();
  if (firstInvalid) {
    firstInvalid->setFocus();
  }
  return firstInvalid == nullptr;
}

void FormFeedback::showStatus(const QString &message, bool ok,
                             const QList<QLineEdit *> &relatedInputs)
{
  _statusMessage = message;
  _statusOk = ok;
  _statusInputs = relatedInputs;
  if (_statusInputs.isEmpty()) {
    for (const Field &field : _fields) {
      _statusInputs.append(field.input);
    }
  }
  refresh();
}

void FormFeedback::clearStatus()
{
  _statusMessage.clear();
  _statusOk = true;
  _statusInputs.clear();
  refresh();
}

void FormFeedback::reset()
{
  _statusMessage.clear();
  _statusOk = true;
  _statusInputs.clear();
  for (Field &field : _fields) {
    field.checked = false;
    field.error.clear();
    field.input->setProperty("validationError", false);
    repolish(field.input);
  }
  refresh();
}

void FormFeedback::refresh()
{
  QString message = _statusMessage;
  bool ok = _statusOk;
  for (const Field &field : _fields) {
    if (!field.error.isEmpty()) {
      message = field.error;
      ok = false;
      break;
    }
  }
  _label->setProperty("state", ok ? "normal" : "err");
  _label->setText(message);
  repolish(_label);
}
