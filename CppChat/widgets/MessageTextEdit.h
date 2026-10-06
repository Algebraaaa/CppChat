#ifndef MESSAGETEXTEDIT_H
#define MESSAGETEXTEDIT_H

#include "common/global.h"
#include <QTextEdit>
#include <QVector>
#include <QHash>

class MessageTextEdit : public QTextEdit
{
  Q_OBJECT
public:
  explicit MessageTextEdit(QWidget *parent = nullptr);

  ~MessageTextEdit();

  QVector<MsgInfo> messages() const;
  void setMessages(const QVector<MsgInfo> &messages);

  void insertFileFromUrl(const QStringList &urls);
  void insertImage(const QImage &image);
  void insertEmoji(const QString &id);
  bool attachmentsReady() const;
public slots:
  void clear();
signals:
  void send();
  void attachmentError(const QString &message);
  void attachmentsChanged();

protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  void keyPressEvent(QKeyEvent *e) override;
  bool canInsertFromMimeData(const QMimeData *source) const override;
  void insertFromMimeData(const QMimeData *source) override;

private:
  void insertImageData(const QByteArray &bytes);
  void insertPrepared(const std::shared_ptr<PreparedAttachment> &attachment);
  void updatePreview(const std::shared_ptr<PreparedAttachment> &attachment);
  QHash<QString, std::shared_ptr<PreparedAttachment>> _attachments;
};

#endif // MESSAGETEXTEDIT_H
