#pragma once
#include "common/userdata.h"
#include <QObject>
#include <QPointer>
#include <QSet>

class MessageMenu;
class QDialog;
class QWidget;

// Message-level UI commands. Storage and synchronization retain their existing owners.
class MessageActions : public QObject {
  Q_OBJECT
public:
  explicit MessageActions(QWidget *owner);
  void SetConversation(std::shared_ptr<ChatThreadData> data);
  void CloseReader();
  void Bind(MessageMenu *menu, std::shared_ptr<ChatDataBase> message);
signals:
  void quoteRequested(int thread, QString sender, QString text);
  void status(int thread, QString text);

private:
  bool IsCurrent(const std::shared_ptr<ChatThreadData> &data,
                 const std::shared_ptr<ChatDataBase> &message) const;
  QString Sender(int uid) const;
  void Read(const std::shared_ptr<ChatDataBase> &message);
  void Delete(const std::shared_ptr<ChatDataBase> &message);
  QWidget *_owner;
  QPointer<QDialog> _reader;
  int _readerMessage = 0;
  int _uid = 0;
  quint64 _generation = 0;
  std::shared_ptr<ChatThreadData> _data;
  QSet<QString> _deleting;
};
