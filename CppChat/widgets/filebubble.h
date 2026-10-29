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

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  enum class Action { None, Save, Open };
  void Activate(Action action);
  void Save(bool openAfterSave);
  void Ready(const QByteArray &bytes, const QString &error);
  QString _id;
  FileAttachment::Document _document;
  QPushButton *_save;
  QPushButton *_open;
  QLabel *_status;
  QLabel *_name;
  quint64 _generation = 0;
  bool _loading = false;
  Action _afterLoad = Action::None;
  QString _savedPath;
};
