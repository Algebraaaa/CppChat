#ifndef MESSAGETEXTEDIT_H
#define MESSAGETEXTEDIT_H

#include "common/global.h"
#include <QHash>
#include <QTextEdit>
#include <QVector>

class MessageTextEdit : public QTextEdit {
  Q_OBJECT
public:
  explicit MessageTextEdit(QWidget *parent = nullptr);

  ~MessageTextEdit();

  QVector<MsgInfo> messages() const;
  void setMessages(const QVector<MsgInfo> &messages);

  void insertFileFromUrl(const QStringList &urls);
  void insertImage(const QImage &image);
  void insertEmoji(const QString &id);
  void insertQuote(const QString &sender, const QString &text);
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
  void inputMethodEvent(QInputMethodEvent *event) override;
  void changeEvent(QEvent *event) override;
  bool canInsertFromMimeData(const QMimeData *source) const override;
  void insertFromMimeData(const QMimeData *source) override;

private:
  void UpdateSendShortcut();
  bool _controlEnter = false;
  bool _composing = false;
  void insertImageData(const QByteArray &bytes);
  void insertPrepared(const std::shared_ptr<PreparedAttachment> &attachment);
  void updatePreview(const std::shared_ptr<PreparedAttachment> &attachment);
  QHash<QString, std::shared_ptr<PreparedAttachment>> _attachments;
};

#endif // MESSAGETEXTEDIT_H
