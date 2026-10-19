#include "quickactionmenu.h"
#include "ui_quickactionmenu.h"
#include <QColor>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>

QuickActionMenu::QuickActionMenu(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
      ui(new Ui::QuickActionMenu)
{
  ui->setupUi(this);
  setAttribute(Qt::WA_TranslucentBackground);
  auto *shadow = new QGraphicsDropShadowEffect(ui->quick_action_panel);
  shadow->setBlurRadius(18);
  shadow->setOffset(0, 3);
  shadow->setColor(QColor(0, 0, 0, 32));
  ui->quick_action_panel->setGraphicsEffect(shadow);
  connect(ui->add_friend_btn, &QPushButton::clicked, this, [this] {
    hide();
    emit addFriendRequested();
  });
  connect(ui->group_chat_btn, &QPushButton::clicked, this, [this] {
    hide();
    emit createGroupRequested();
  });
  connect(ui->note_btn, &QPushButton::clicked, this, [this] {
    hide();
    emit notesRequested();
  });
}

QuickActionMenu::~QuickActionMenu() { delete ui; }

void QuickActionMenu::Popup(const QPoint &position)
{
  ensurePolished();
  adjustSize();
  QPoint target = position;
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
  setFocus(Qt::PopupFocusReason);
}

void QuickActionMenu::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_Escape) {
    hide();
    event->accept();
    return;
  }
  const QList<QPushButton *> buttons{ui->group_chat_btn, ui->add_friend_btn, ui->note_btn};
  if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
    int index = -1;
    for (int i = 0; i < buttons.size(); ++i)
      if (buttons[i]->hasFocus())
        index = i;
    if (index < 0)
      index = event->key() == Qt::Key_Down ? 0 : buttons.size() - 1;
    else
      index = (index + (event->key() == Qt::Key_Down ? 1 : buttons.size() - 1)) % buttons.size();
    buttons[index]->setFocus(Qt::TabFocusReason);
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    for (auto *button : buttons)
      if (button->hasFocus()) {
        button->click();
        event->accept();
        return;
      }
  QWidget::keyPressEvent(event);
}
