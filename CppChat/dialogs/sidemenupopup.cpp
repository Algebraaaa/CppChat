#include "sidemenupopup.h"
#include "ui_sidemenupopup.h"
#include <QColor>
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QLayout>
#include <QPushButton>
#include <QScreen>
#include <algorithm>

SideMenuPopup::SideMenuPopup(QWidget *parent) : QDialog(parent), ui(new Ui::SideMenuPopup)
{
  ui->setupUi(this);
  // Popup 自动处理点击外部关闭；透明窗口为面板圆角和阴影留出空间。
  setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  auto *shadow = new QGraphicsDropShadowEffect(ui->menu_panel);
  shadow->setBlurRadius(18);
  shadow->setOffset(0, 3);
  shadow->setColor(QColor(0, 0, 0, 40));
  ui->menu_panel->setGraphicsEffect(shadow);

  _buttons = {ui->live_companion_btn, ui->chat_files_btn, ui->chat_history_btn,
              ui->lock_btn,           ui->feedback_btn,   ui->settings_btn};
  for (auto *button : _buttons) {
    button->installEventFilter(this);
    button->setAccessibleName(button->text());
    connect(button, &QPushButton::clicked, this, [this, button] {
      hide(); // 先收起菜单，再由主窗口处理入口，避免占位窗口与 Popup 抢焦点。
      emit sig_action_requested(button->text());
    });
  }
}

SideMenuPopup::~SideMenuPopup()
{
  delete ui;
}

void SideMenuPopup::PopupAt(QWidget *anchor)
{
  if (!anchor)
    return;
  ensurePolished();
  layout()->activate();
  adjustSize();

  // 面板贴近按钮右侧，底部略低于按钮；窗口自身的边距是阴影区域。
  const QPoint edge = anchor->mapToGlobal(QPoint(anchor->width() + 8, anchor->height() + 8));
  QPoint position(edge.x() - 12, edge.y() + 14 - height());
  if (auto *targetScreen = anchor->screen()) {
    const QRect available = targetScreen->availableGeometry();
    position.setX(std::clamp(position.x(), available.left(),
                             std::max(available.left(), available.right() - width() + 1)));
    position.setY(std::clamp(position.y(), available.top(),
                             std::max(available.top(), available.bottom() - height() + 1)));
  }
  move(position);
  show();
  raise();
  _buttons.first()->setFocus(Qt::PopupFocusReason);
}

bool SideMenuPopup::eventFilter(QObject *watched, QEvent *event)
{
  // 按钮可能自行处理方向键，因此在局部过滤器中统一菜单键盘导航。
  if (event->type() == QEvent::KeyPress) {
    auto *keyEvent = static_cast<QKeyEvent *>(event);
    switch (keyEvent->key()) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_Return:
    case Qt::Key_Enter:
      keyPressEvent(keyEvent);
      return true;
    default:
      break;
    }
  }
  return QDialog::eventFilter(watched, event);
}

void SideMenuPopup::keyPressEvent(QKeyEvent *event)
{
  int index = _buttons.indexOf(qobject_cast<QPushButton *>(focusWidget()));
  if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up || event->key() == Qt::Key_Home ||
      event->key() == Qt::Key_End) {
    if (event->key() == Qt::Key_Home)
      index = 0;
    else if (event->key() == Qt::Key_End)
      index = _buttons.size() - 1;
    else if (index < 0)
      index = event->key() == Qt::Key_Up ? _buttons.size() - 1 : 0;
    else
      index = (index + (event->key() == Qt::Key_Down ? 1 : _buttons.size() - 1)) % _buttons.size();
    _buttons.at(index)->setFocus(Qt::OtherFocusReason);
    event->accept();
    return;
  }
  if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && index >= 0) {
    _buttons.at(index)->click();
    event->accept();
    return;
  }
  QDialog::keyPressEvent(event); // Escape 沿用 QDialog 的关闭行为。
}
