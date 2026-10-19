#include "chatwindow.h"
#include "chatpage.h"
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

ChatWindow::ChatWindow(QWidget *parent) : QDialog(parent)
{
  setObjectName("detachedChatWindow");
  setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                 Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint |
                 Qt::WindowCloseButtonHint);
  setModal(false);
  auto *body = new QVBoxLayout(this);
  body->setContentsMargins(0, 0, 0, 0);
  auto *dock = new QPushButton(tr("返回主窗口"), this);
  dock->setObjectName("dockChatButton");
  dock->setAutoDefault(false);
  body->addWidget(dock, 0, Qt::AlignRight);
  connect(dock, &QPushButton::clicked, this, &ChatWindow::dockRequested);
  resize(QSize(800, 700).boundedTo(screen()->availableGeometry().size()));
}

void ChatWindow::Host(ChatPage *page)
{
  layout()->addWidget(page);
  page->show();
  showNormal();
  raise();
  activateWindow();
}

void ChatWindow::done(int)
{
  emit dockRequested(); // Close and Escape dock the live editor instead of destroying it.
}
