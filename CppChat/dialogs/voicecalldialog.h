#pragma once
#include <QDialog>
#include <QElapsedTimer>
#include <QTimer>
class QLabel;
class TcpMgr;
class QPushButton;
class VoiceCallService;
class VoiceAudio;
class VideoMedia;
class VoiceCallDialog : public QDialog {
  Q_OBJECT
public:
  explicit VoiceCallDialog(QWidget *parent = nullptr);
  ~VoiceCallDialog() override;
  void call(int peer, bool video = false);
  void finish();
  void reject() override;
signals:
  void status(QString message);

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  void refresh();
  void connectVideo(TcpMgr *tcp);
  VoiceCallService *service_;
  VoiceAudio *audio_;
  VideoMedia *video_;
  QLabel *local_, *remote_;
  QPushButton *camera_;
  QLabel *name_, *state_;
  QPushButton *accept_, *end_, *mute_;
  QTimer timer_;
  QElapsedTimer duration_;
  bool audioStarted_ = false;
};
