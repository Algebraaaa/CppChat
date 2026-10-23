#pragma once
#include "bubbleframe.h"
#include "common/fileattachment.h"
class QPushButton;
class QLabel;
class FileBubble : public BubbleFrame {
  Q_OBJECT
public:
  explicit FileBubble(ChatRole role, QWidget *parent = nullptr);
  void Load(const QString &fileId);
private:
  void FetchOrSave();
  void Ready(const QByteArray &bytes, const QString &error);
  QString _id;
  FileAttachment::Document _document;
  QPushButton *_save;
  QLabel *_status;
  quint64 _generation = 0;
  bool _loading = false;
};
