#pragma once
#include <QDialog>

class ChatPage;

// Hosts the existing editor; never creates another sender, draft cache or call service.
class ChatWindow : public QDialog {
  Q_OBJECT
public:
  explicit ChatWindow(QWidget *parent);
  void Host(ChatPage *page);
signals:
  void dockRequested();

protected:
  void done(int result) override;
};
