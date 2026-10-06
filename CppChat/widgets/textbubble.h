#ifndef TEXTBUBBLE_H
#define TEXTBUBBLE_H

#include <QTextEdit>
#include "bubbleframe.h"
#include <QHBoxLayout>

class TextBubble : public BubbleFrame
{
  Q_OBJECT
public:
  TextBubble(ChatRole role, const QString &text, QWidget *parent = nullptr);

protected:
  bool eventFilter(QObject *o, QEvent *e) override;

private:
  void scheduleHeightUpdate();
  void adjustTextHeight();
  void setPlainText(const QString &text);
  QTextEdit *m_pTextEdit;
  bool _heightUpdatePending = false;
};

#endif // TEXTBUBBLE_H
