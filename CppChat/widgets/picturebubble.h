#ifndef PICTUREBUBBLE_H
#define PICTUREBUBBLE_H

#include "bubbleframe.h"
#include <QByteArray>
#include <QHBoxLayout>
#include <QPixmap>
#include <QPointer>
#include <QString>
class QDialog;
class QLabel;
class QMouseEvent;
class PictureBubble : public BubbleFrame {
  Q_OBJECT
public:
  explicit PictureBubble(ChatRole role, QWidget *parent = nullptr);
  void loadImage(const QString &fileId);
  void copyOriginal();
  void setError(const QString &message);
  void setProgress(qint64 done, qint64 total);
  bool hasImage() const { return !_preview.isNull(); }
signals:
  void retryRequested();

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private:
  void loadPreview(const QByteArray &bytes);
  void setPreview(const QImage &image);
  QLabel *_label = nullptr;
  QPixmap _preview;
  QString _fileId;
  quint64 _revision = 0;
  bool _copying = false;
  QPointer<QDialog> _previewDialog;
};

#endif // PICTUREBUBBLE_H
