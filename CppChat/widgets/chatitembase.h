#ifndef CHATITEMBASE_H
#define CHATITEMBASE_H

#include "common/global.h"
#include <QGridLayout>
#include <QLabel>
#include <QWidget>
class BubbleFrame;

class ChatItemBase : public QWidget {
  Q_OBJECT
public:
  explicit ChatItemBase(ChatRole role, QWidget *parent = nullptr);
  void setUserName(const QString &name);
  void setUserIcon(const QPixmap &icon);
  void setWidget(QWidget *w);
  void setStatus(int status);

private:
  ChatRole m_role;
  QLabel *m_pStatusLabel = nullptr;
  QLabel *m_pNameLabel;
  QLabel *m_pIconLabel;
  QWidget *m_pBubble;
};

#endif // CHATITEMBASE_H
