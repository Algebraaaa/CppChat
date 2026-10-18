#pragma once
#include "common/favorite.h"
#include <QObject>
#include <QPointer>
#include <QVector>
class ChatPage;
class ChatSyncService;
class ChatThreadData;
class QDialog;
class QLabel;
class QListWidget;
class QPushButton;
class QWidget;

// One explicit destination, with correlated private-thread creation and account cancellation.
class MessageForwarder : public QObject {
  Q_OBJECT
public:
  MessageForwarder(QWidget *owner, ChatPage *page, ChatSyncService *sync);
  void Begin(QVector<StoredFavorite> snapshots);
  void Reset();
signals:
  void status(QString message);

private:
  void Choose();
  void Send(int thread, int other);
  QWidget *_owner;
  ChatPage *_page;
  ChatSyncService *_sync;
  QPointer<QDialog> _dialog;
  QListWidget *_list = nullptr;
  QLabel *_status = nullptr;
  QPushButton *_send = nullptr;
  QVector<StoredFavorite> _messages;
  quint64 _generation = 0;
  int _uid = 0, _pendingUid = 0;
};
