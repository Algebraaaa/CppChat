#pragma once
#include <QByteArray>
#include <QDialog>
class QLabel;
class QPushButton;
class VoiceMessageDialog : public QDialog {
  Q_OBJECT
public:
  explicit VoiceMessageDialog(QWidget *parent = nullptr);
  ~VoiceMessageDialog() override;
  void SetSending(bool sending);
  bool Sending() const { return _sending; }
signals:
  void sendRequested(QByteArray wave);

private:
  void CancelDevices();
  void Refresh();
  QByteArray _wave;
  QString _previewKey;
  QLabel *_status;
  QPushButton *_record, *_stop, *_preview, *_send;
  bool _recording = false;
  bool _sending = false;
};
