#include "messagemenu.h"
#include "ui_messagemenu.h"
#include <QColor>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHideEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QPixmap>
#include <QScreen>

MessageMenu::MessageMenu(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
      ui(new Ui::MessageMenu)
{
  ui->setupUi(this);
  setAttribute(Qt::WA_TranslucentBackground);
  auto *shadow = new QGraphicsDropShadowEffect(ui->menu_panel);
  shadow->setBlurRadius(16);
  shadow->setOffset(0, 3);
  shadow->setColor(QColor(0, 0, 0, 35));
  ui->menu_panel->setGraphicsEffect(shadow);
  _buttons = {ui->copy_btn, ui->read_btn, ui->translate_btn, ui->search_btn,
              ui->forward_btn, ui->favorite_btn, ui->select_btn, ui->remind_btn,
              ui->quote_btn, ui->delete_btn, ui->retry_btn};
  // 无图标的菜单项也保留图标列，所有文字从同一位置开始。
  QPixmap emptyIcon(20, 20);
  emptyIcon.fill(Qt::transparent);
  for (auto *button : _buttons) {
    if (button->icon().isNull())
      button->setIcon(QIcon(emptyIcon));
  }
  connect(ui->copy_btn, &QPushButton::clicked, this, [this] {
    hide();
    emit copyRequested();
  });
  connect(ui->retry_btn, &QPushButton::clicked, this, [this] {
    hide();
    emit retryRequested();
  });
  for (auto *button : {ui->read_btn, ui->translate_btn, ui->search_btn, ui->forward_btn,
                       ui->favorite_btn, ui->select_btn, ui->remind_btn, ui->quote_btn,
                       ui->delete_btn}) {
    connect(button, &QPushButton::clicked, this, [this, button] {
      hide();
      emit placeholderRequested(button->text());
    });
  }
}

MessageMenu::~MessageMenu()
{
  delete ui;
}

void MessageMenu::Popup(const QPoint &position, bool canCopy, bool showRetry, bool canRetry)
{
  ui->copy_btn->setEnabled(canCopy);
  ui->retry_btn->setVisible(showRetry);
  ui->retry_btn->setEnabled(canRetry);
  ui->retry_separator->setVisible(showRetry);
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
  // 打开菜单时不把第一项画成悬停；按方向键后再显示键盘焦点。
  setFocus(Qt::PopupFocusReason);
}

void MessageMenu::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_Escape) {
    hide();
    event->accept();
    return;
  }
  QList<QPushButton *> available;
  for (auto *button : _buttons)
    if (button->isVisible() && button->isEnabled())
      available.append(button);
  if ((event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) && !available.isEmpty()) {
    int index = -1;
    for (int i = 0; i < available.size(); ++i)
      if (available[i]->hasFocus())
        index = i;
    if (index < 0)
      index = event->key() == Qt::Key_Down ? 0 : available.size() - 1;
    else
      index = (index + (event->key() == Qt::Key_Down ? 1 : available.size() - 1)) % available.size();
    available[index]->setFocus(Qt::TabFocusReason);
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    for (auto *button : available)
      if (button->hasFocus()) {
        button->click();
        event->accept();
        return;
      }
  }
  QWidget::keyPressEvent(event);
}

void MessageMenu::hideEvent(QHideEvent *event)
{
  QWidget::hideEvent(event);
  deleteLater();
}
