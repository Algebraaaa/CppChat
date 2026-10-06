#include "conversationmenu.h"
#include "ui_conversationmenu.h"
#include <QColor>
#include <QGuiApplication>
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QIcon>
#include <QScreen>

ConversationMenu::ConversationMenu(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
      ui(new Ui::ConversationMenu)
{
  ui->setupUi(this);
  setAttribute(Qt::WA_TranslucentBackground);
  auto *shadow = new QGraphicsDropShadowEffect(ui->menu_panel);
  shadow->setBlurRadius(14);
  shadow->setOffset(0, 2);
  shadow->setColor(QColor(0, 0, 0, 30));
  ui->menu_panel->setGraphicsEffect(shadow);
  connect(ui->pin_btn, &QPushButton::clicked, this, [this] {
    hide();
    emit sig_pin_changed(_threadId, !_pinned);
  });
  connect(ui->read_btn, &QPushButton::clicked, this, [this] {
    hide();
    emit sig_unread_changed(_threadId, !_unread);
  });
  for (auto *button : {ui->mute_btn, ui->window_btn, ui->hide_btn, ui->delete_btn}) {
    connect(button, &QPushButton::clicked, this, [this, button] {
      hide();
      emit sig_placeholder(button->text());
    });
  }
}

ConversationMenu::~ConversationMenu()
{
  delete ui;
}

void ConversationMenu::Popup(int threadId, bool pinned, bool unread, const QPoint &position)
{
  _threadId = threadId;
  _pinned = pinned;
  _unread = unread;
  ui->pin_btn->setText(pinned ? tr("取消置顶") : tr("置顶"));
  ui->pin_btn->setIcon(
      QIcon(pinned ? ":/icons/conversation-pin-off.svg" : ":/icons/conversation-pin.svg"));
  ui->read_btn->setText(unread ? tr("标为已读") : tr("标为未读"));
  ui->read_btn->setIcon(
      QIcon(unread ? ":/icons/conversation-mail-open.svg" : ":/icons/conversation-mail.svg"));
  ensurePolished();
  adjustSize();
  QPoint target = position - QPoint(8, 8);
  if (const auto *screen = QGuiApplication::screenAt(position)) {
    const QRect available = screen->availableGeometry();
    target.setX(qBound(available.left(), target.x(),
                       qMax(available.left(), available.right() - width() + 1)));
    target.setY(qBound(available.top(), target.y(),
                       qMax(available.top(), available.bottom() - height() + 1)));
  }
  move(target);
  show();
  raise();
  ui->pin_btn->setFocus(Qt::PopupFocusReason);
}

void ConversationMenu::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_Escape) {
    hide();
    event->accept();
    return;
  }
  const QList<QPushButton *> buttons{ui->pin_btn,    ui->read_btn, ui->mute_btn,
                                     ui->window_btn, ui->hide_btn, ui->delete_btn};
  if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
    int index = 0;
    for (int i = 0; i < buttons.size(); ++i)
      if (buttons[i]->hasFocus())
        index = i;
    index = (index + (event->key() == Qt::Key_Down ? 1 : buttons.size() - 1)) % buttons.size();
    buttons[index]->setFocus(Qt::TabFocusReason);
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) {
    for (auto *button : buttons)
      if (button->hasFocus()) {
        button->click();
        event->accept();
        return;
      }
  }
  QWidget::keyPressEvent(event);
}
