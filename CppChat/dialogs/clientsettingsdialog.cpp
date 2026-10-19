#include "clientsettingsdialog.h"
#include "common/clientpreferences.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

ClientSettingsDialog::ClientSettingsDialog(QWidget *parent, ClientPreferences *preferences)
    : QDialog(parent), _preferences(preferences ? preferences : ClientPreferences::instance())
{
  setObjectName("clientSettingsDialog");
  setProperty("groupDialog", true);
  setWindowTitle(tr("本机设置"));
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  setModal(false);
  auto *body = new QVBoxLayout(this);
  auto *hint = new QLabel(tr("设置仅保存在本机，不上传服务器；保存后立即生效。"), this);
  hint->setWordWrap(true);
  body->addWidget(hint);
  auto *form = new QFormLayout;
  _appearance = new QComboBox(this);
  _appearance->setObjectName("appearanceSetting");
  _appearance->addItem(tr("跟随 Windows / 系统"), int(ClientPreferences::Appearance::System));
  _appearance->addItem(tr("浅色"), int(ClientPreferences::Appearance::Light));
  _appearance->addItem(tr("深色"), int(ClientPreferences::Appearance::Dark));
  form->addRow(tr("外观"), _appearance);
  _sendKey = new QComboBox(this);
  _sendKey->setObjectName("sendKeySetting");
  _sendKey->addItem(tr("Enter 发送，Shift/Ctrl+Enter 换行"),
                    int(ClientPreferences::SendKey::Enter));
  _sendKey->addItem(tr("Ctrl+Enter 发送，Enter 换行"),
                    int(ClientPreferences::SendKey::ControlEnter));
  form->addRow(tr("发送快捷键"), _sendKey);
  _startupUpdates = new QCheckBox(tr("启动时自动检查更新"), this);
  _startupUpdates->setObjectName("startupUpdatesSetting");
  form->addRow(_startupUpdates);
  form->addRow(tr("当前版本"), new QLabel(QStringLiteral(CPPCHAT_VERSION), this));
  body->addLayout(form);
  auto *check = new QPushButton(tr("立即检查更新"), this);
  check->setObjectName("settingsCheckUpdates");
  check->setAutoDefault(false);
  body->addWidget(check, 0, Qt::AlignLeft);
  connect(check, &QPushButton::clicked, this, &ClientSettingsDialog::checkUpdatesRequested);
  _status = new QLabel(this);
  _status->setObjectName("settingsStatus");
  _status->setWordWrap(true);
  _status->setTextFormat(Qt::PlainText);
  body->addWidget(_status);
  auto *buttons = new QHBoxLayout;
  auto *defaults = new QPushButton(tr("恢复默认选项"), this);
  defaults->setObjectName("defaultSettings");
  buttons->addWidget(defaults);
  connect(defaults, &QPushButton::clicked, this, [this] {
    _sendKey->setCurrentIndex(0);
    _startupUpdates->setChecked(true);
    _appearance->setCurrentIndex(0);
    _status->setText(tr("点击保存后恢复默认设置"));
  });
  buttons->addStretch();
  auto *save = new QPushButton(tr("保存"), this);
  save->setObjectName("saveSettings");
  save->setProperty("primary", true);
  buttons->addWidget(save);
  auto *close = new QPushButton(tr("关闭"), this);
  close->setObjectName("closeSettings");
  buttons->addWidget(close);
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  connect(save, &QPushButton::clicked, this, [this] {
    ClientPreferences::State state;
    state.sendKey = ClientPreferences::SendKey(_sendKey->currentData().toInt());
    state.checkUpdatesOnStartup = _startupUpdates->isChecked();
    state.appearance = ClientPreferences::Appearance(_appearance->currentData().toInt());
    QString error;
    _status->setText(_preferences->Save(state, error) ? tr("已保存，本机设置立即生效") : error);
  });
  body->addLayout(buttons);
  connect(_preferences, &ClientPreferences::changed, this, &ClientSettingsDialog::Reload);
  Reload();
  resize(QSize(620, 320).boundedTo(screen()->availableGeometry().size()));
}

void ClientSettingsDialog::Reload()
{
  const auto state = _preferences->Current();
  _sendKey->setCurrentIndex(_sendKey->findData(int(state.sendKey)));
  _startupUpdates->setChecked(state.checkUpdatesOnStartup);
  _appearance->setCurrentIndex(_appearance->findData(int(state.appearance)));
  _status->setText(_preferences->LoadError());
}
