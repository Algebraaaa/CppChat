#ifndef CHATSETTINGSPANEL_H
#define CHATSETTINGSPANEL_H

#include "common/userdata.h"
#include <QPointer>
#include <QRect>
#include <QWidget>

class QPropertyAnimation;
namespace Ui {
class ChatSettingsPanel;
}

// 不参与聊天页布局：浮在内容上，通过动画改变露出的比例。
class ChatSettingsPanel : public QWidget {
  Q_OBJECT
  Q_PROPERTY(qreal revealProgress READ revealProgress WRITE setRevealProgress)
public:
  explicit ChatSettingsPanel(QWidget *parent = nullptr);
  ~ChatSettingsPanel() override;
  void Toggle();
  void Close(bool animated = true);
  void SetAvailableRect(const QRect &rect);
  void SetToggleButton(QWidget *button);
  void SetUser(const std::shared_ptr<UserInfo> &user);
  void SetPinned(bool pinned);
  qreal revealProgress() const;
  void setRevealProgress(qreal progress);

signals:
  void sig_history_requested();
  void sig_clear_history();
  void sig_pin_changed(bool pinned);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  void AnimateTo(qreal target);
  void ShowPlaceholder(const QString &feature);
  Ui::ChatSettingsPanel *ui;
  QPropertyAnimation *_animation;
  QPointer<QWidget> _toggleButton;
  QRect _availableRect;
  qreal _progress = 0;
  bool _openRequested = false;
};
#endif
