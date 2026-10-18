#pragma once
#include "bubbleframe.h"
#include <QByteArray>
class QPushButton;
class QLabel;
class AudioBubble : public BubbleFrame {
  Q_OBJECT
public:
  explicit AudioBubble(ChatRole role, QWidget *parent = nullptr);
  ~AudioBubble() override;
  void Load(const QString &fileId);
  void SetSnapshot(const QByteArray &wave);

protected:
  void hideEvent(QHideEvent *event) override;

private:
  void Toggle();
  void Ready(const QByteArray &wave, const QString &error);
  QString _file, _key;
  QByteArray _wave;
  QPushButton *_play;
  QLabel *_status;
  quint64 _revision = 0, _generation = 0;
  bool _loading = false;
};
