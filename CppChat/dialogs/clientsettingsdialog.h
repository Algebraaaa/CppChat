#pragma once
#include <QDialog>
class ClientPreferences;
class QComboBox;
class QCheckBox;
class QLabel;

class ClientSettingsDialog : public QDialog {
  Q_OBJECT
public:
  explicit ClientSettingsDialog(QWidget *parent = nullptr,
                                ClientPreferences *preferences = nullptr);
signals:
  void checkUpdatesRequested();

private:
  ClientPreferences *_preferences;
  QComboBox *_sendKey;
  QComboBox *_appearance;
  QCheckBox *_startupUpdates;
  QLabel *_status;
  void Reload();
};
