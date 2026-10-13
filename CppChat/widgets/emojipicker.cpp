#include "emojipicker.h"
#include "common/emojicatalog.h"
#include "common/runtimepaths.h"
#include <QButtonGroup>
#include <QDir>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QString preferencesPath()
{
  const QString directory = RuntimePaths::preferencesDirectory();
  QDir().mkpath(directory);
  return directory + "/emojis.ini";
}

QStringList validIds(const QStringList &ids, int limit)
{
  QStringList result;
  for (const auto &id : ids) {
    if (EmojiCatalog::FindById(id) && !result.contains(id))
      result.append(id);
    if (result.size() >= limit)
      break;
  }
  return result;
}
} // namespace

EmojiPicker::EmojiPicker(QWidget *parent) : QFrame(parent)
{
  setObjectName("emoji_popup");
  setFocusPolicy(Qt::StrongFocus);
  setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(12, 12, 12, 12);
  auto *panel = new QFrame(this);
  panel->setObjectName("emoji_panel");
  panel->setAttribute(Qt::WA_StyledBackground);
  auto *shadow = new QGraphicsDropShadowEffect(panel);
  shadow->setBlurRadius(20);
  shadow->setOffset(0, 3);
  shadow->setColor(QColor(0, 0, 0, 35));
  panel->setGraphicsEffect(shadow);
  outer->addWidget(panel);
  auto *layout = new QVBoxLayout(panel);
  layout->setContentsMargins(0, 18, 0, 0);
  layout->setSpacing(0);

  _search = new QLineEdit(panel);
  _search->setObjectName("emoji_search");
  _search->setPlaceholderText(tr("搜索表情，例如：微笑、捂脸、玫瑰"));
  _search->setAccessibleName(tr("搜索表情"));
  _search->setClearButtonEnabled(true);
  _search->setMaxLength(40);
  _search->hide();
  layout->addWidget(_search);
  _searchTimer = new QTimer(this);
  _searchTimer->setSingleShot(true);
  _searchTimer->setInterval(150);
  connect(_searchTimer, &QTimer::timeout, this, &EmojiPicker::RebuildContent);
  connect(_search, &QLineEdit::textChanged, this, [this] {
    _scroll->widget()->hide();
    _searchTimer->start();
  });
  _buttonHost = new QWidget(this);
  _buttonHost->hide();

  _scroll = new QScrollArea(panel);
  _scroll->setObjectName("emoji_scroll");
  _scroll->setWidgetResizable(true);
  _scroll->setFrameShape(QFrame::NoFrame);
  _scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *content = new QWidget;
  content->setObjectName("emoji_content");
  _contentLayout = new QVBoxLayout(content);
  _contentLayout->setContentsMargins(24, 4, 24, 16);
  _contentLayout->setSpacing(16);
  _scroll->setWidget(content);
  layout->addWidget(_scroll, 1);

  auto *toolbar = new QFrame(panel);
  toolbar->setObjectName("emoji_toolbar");
  toolbar->setFixedHeight(70);
  auto *bar = new QHBoxLayout(toolbar);
  bar->setContentsMargins(22, 8, 22, 8);
  bar->setSpacing(10);
  auto *searchButton = new QToolButton(toolbar);
  _searchButton = searchButton;
  searchButton->setFocusPolicy(Qt::TabFocus);
  searchButton->setObjectName("emoji_tab");
  searchButton->setIcon(QIcon(":/emojis/search.svg"));
  searchButton->setIconSize(QSize(26, 26));
  searchButton->setFixedSize(46, 46);
  searchButton->setToolTip(tr("搜索表情"));
  searchButton->setAccessibleName(searchButton->toolTip());
  searchButton->setCursor(Qt::PointingHandCursor);
  searchButton->setCheckable(true);
  connect(searchButton, &QToolButton::toggled, this, [this](bool checked) {
    _search->setVisible(checked);
    if (checked)
      _search->setFocus();
    else {
      _search->clear();
      setFocus(Qt::OtherFocusReason);
    }
  });
  bar->addWidget(searchButton);

  const QStringList titles{tr("所有表情"), tr("收藏表情"), tr("Telegram")};
  const QStringList icons{":/icons/smile-normal.svg", ":/emojis/heart.svg",
                          ":/emojis/telegram/1f600.png"};
  auto *categoryGroup = new QButtonGroup(this);
  categoryGroup->setExclusive(true);
  for (int i = 0; i < titles.size(); ++i) {
    auto *button = new QToolButton(toolbar);
    button->setObjectName("emoji_tab");
    button->setFocusPolicy(Qt::TabFocus);
    categoryGroup->addButton(button, i);
    button->setFixedSize(46, 46);
    button->setIcon(QIcon(icons[i]));
    button->setIconSize(QSize(28, 28));
    if (i != 2)
      button->setToolTip(titles[i]);
    button->setAccessibleName(titles[i]);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    _tabs.append(button);
    bar->addWidget(button);
    connect(button, &QToolButton::clicked, this, [this, i] { SelectCategory(i); });
  }
  bar->addStretch();
  auto *next = new QToolButton(toolbar);
  next->setObjectName("emoji_tab");
  next->setFocusPolicy(Qt::TabFocus);
  next->setFixedSize(38, 46);
  next->setIcon(QIcon(":/icons/contact-chevron-right.svg"));
  next->setIconSize(QSize(22, 22));
  next->setToolTip(tr("下一分类"));
  next->setAccessibleName(next->toolTip());
  next->setCursor(Qt::PointingHandCursor);
  connect(next, &QToolButton::clicked, this,
          [this] { SelectCategory((_category + 1) % _tabs.size()); });
  bar->addWidget(next);
  layout->addWidget(toolbar);
  SelectCategory(0);
}

void EmojiPicker::PopupAt(QWidget *anchor, int uid)
{
  if (!anchor)
    return;
  if (isVisible()) {
    hide();
    return;
  }
  _uid = uid;
  QSettings settings(preferencesPath(), QSettings::IniFormat);
  settings.beginGroup(QString("accounts/%1").arg(_uid));
  _recent = validIds(settings.value("recent").toStringList(), 10);
  _favorites = validIds(settings.value("favorites").toStringList(), EmojiCatalog::Entries().size());
  const QRect available = anchor->screen()->availableGeometry();
  const int popupWidth = std::min(724, available.width() - 16);
  const int popupHeight = std::min(724, available.height() - 24);
  // 分类栏在窄屏上使用较小的按钮，表情网格自动减少列数。
  const int tabSize = popupWidth < 500 ? 30 : 46;
  _searchButton->setFixedSize(tabSize, 46);
  for (auto *button : _tabs)
    button->setFixedSize(tabSize, 46);
  _columns = std::clamp((popupWidth - 86) / 62, 4, 10);
  resize(popupWidth, popupHeight);
  RebuildContent();
  ensurePolished();
  layout()->activate();
  const QPoint edge = anchor->mapToGlobal(QPoint(-12, -6));
  QPoint position(edge.x(), edge.y() - height());
  if (position.y() < available.top())
    position.setY(available.top() + 8);
  position.setX(std::clamp(position.x(), available.left(), available.right() - width() + 1));
  position.setY(std::clamp(position.y(), available.top(), available.bottom() - height() + 1));
  move(position);
  show();
  raise();
  if (_search->isVisible())
    _search->setFocus(Qt::PopupFocusReason);
  else
    setFocus(Qt::PopupFocusReason);
}

void EmojiPicker::SelectCategory(int index)
{
  _category = index;
  for (int i = 0; i < _tabs.size(); ++i)
    _tabs[i]->setChecked(i == index);
  RebuildContent();
  _scroll->verticalScrollBar()->setValue(0);
}

QWidget *EmojiPicker::CreateGrid(const QVector<const EmojiEntry *> &entries, bool recent)
{
  auto *gridWidget = new QWidget;
  auto *grid = new QGridLayout(gridWidget);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(0);
  grid->setVerticalSpacing(6);
  for (int column = 0; column < _columns; ++column)
    grid->setColumnStretch(column, 1);
  for (int i = 0; i < entries.size(); ++i) {
    const auto *emoji = entries[i];
    const auto key = (recent ? QStringLiteral("recent:") : QStringLiteral("main:")) + emoji->id;
    auto *button = _buttons.value(key);
    if (button) {
      button->setParent(gridWidget);
      button->show();
      grid->addWidget(button, i / _columns, i % _columns, Qt::AlignCenter);
      continue;
    }
    button = new QToolButton(gridWidget);
    _buttons.insert(key, button);
    button->setProperty("emoji_id", emoji->id);
    button->setObjectName("emoji_option");
    button->setFocusPolicy(Qt::TabFocus);
    button->setFixedSize(52, 58);
    button->setIcon(QIcon(emoji->resource));
    button->setIconSize(QSize(34, 34));
    if (emoji->pack != QStringLiteral("telegram-twemoji"))
      button->setToolTip(tr("%1 · 右键收藏").arg(emoji->name));
    button->setAccessibleName(emoji->name);
    button->setCursor(Qt::PointingHandCursor);
    button->setContextMenuPolicy(Qt::CustomContextMenu);
    grid->addWidget(button, i / _columns, i % _columns, Qt::AlignCenter);
    connect(button, &QToolButton::clicked, this, [this, emoji] {
      _recent.removeAll(emoji->id);
      _recent.prepend(emoji->id);
      while (_recent.size() > 10)
        _recent.removeLast();
      SavePreferences();
      hide();
      emit emojiSelected(emoji->id);
    });
    connect(button, &QWidget::customContextMenuRequested, this,
            [this, button, emoji](const QPoint &point) {
              QMenu menu(button);
              const bool favorite = _favorites.contains(emoji->id);
              auto *action = menu.addAction(favorite ? tr("取消收藏") : tr("收藏表情"));
              if (menu.exec(button->mapToGlobal(point)) != action)
                return;
              if (favorite)
                _favorites.removeAll(emoji->id);
              else
                _favorites.append(emoji->id);
              SavePreferences();
              // 等右键菜单回调退出后再重建，避免删除正在发出信号的按钮。
              QMetaObject::invokeMethod(this, [this] { RebuildContent(); }, Qt::QueuedConnection);
            });
  }
  return gridWidget;
}

void EmojiPicker::RebuildContent()
{
  _searchTimer->stop();
  // 按钮和 QIcon 按目录 ID 复用，只重排小型布局容器。
  for (auto *button : _buttons) {
    button->hide();
    button->setParent(_buttonHost);
  }
  while (auto *item = _contentLayout->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  const auto addTitle = [this](const QString &text) {
    auto *label = new QLabel(text);
    label->setObjectName("emoji_title");
    _contentLayout->addWidget(label);
  };
  const auto addHint = [this](const QString &text) {
    auto *label = new QLabel(text);
    label->setObjectName("emoji_empty");
    label->setWordWrap(true);
    label->setMinimumHeight(58);
    _contentLayout->addWidget(label);
  };
  const QString query = _search->text().trimmed();
  if (_category == 0 && query.isEmpty()) {
    addTitle(tr("最近使用"));
    QVector<const EmojiEntry *> recent;
    for (const auto &id : _recent)
      if (const auto *emoji = EmojiCatalog::FindById(id))
        recent.append(emoji);
    if (recent.isEmpty())
      addHint(tr("使用过的表情会显示在这里"));
    else
      _contentLayout->addWidget(CreateGrid(recent, true));
  }
  const QStringList titles{tr("所有表情"), tr("收藏表情"), tr("Telegram")};
  if (!query.isEmpty())
    addTitle(tr("搜索结果"));
  else if (_category != 2)
    addTitle(titles[_category]);
  QVector<const EmojiEntry *> matches;
  for (const auto &emoji : EmojiCatalog::Entries()) {
    if (_category == 1 && !_favorites.contains(emoji.id))
      continue;
    if (_category == 2 && emoji.pack != QStringLiteral("telegram-twemoji"))
      continue;
    if (!query.isEmpty() && !emoji.name.contains(query, Qt::CaseInsensitive) &&
        !emoji.id.contains(query, Qt::CaseInsensitive) &&
        !emoji.aliases.join(' ').contains(query, Qt::CaseInsensitive))
      continue;
    matches.append(&emoji);
  }
  if (!matches.isEmpty())
    _contentLayout->addWidget(CreateGrid(matches));
  else
    addHint(_category == 1 && query.isEmpty() ? tr("右键点击表情，选择“收藏表情”")
                                              : tr("没有找到匹配的表情"));
  _contentLayout->addStretch();
  _scroll->widget()->show();
}

void EmojiPicker::SavePreferences()
{
  if (_uid <= 0)
    return;
  QSettings settings(preferencesPath(), QSettings::IniFormat);
  settings.beginGroup(QString("accounts/%1").arg(_uid));
  settings.setValue("recent", _recent);
  settings.setValue("favorites", _favorites);
}

void EmojiPicker::showEvent(QShowEvent *event)
{
  QFrame::showEvent(event);
  emit visibilityChanged(true);
}

void EmojiPicker::hideEvent(QHideEvent *event)
{
  _searchTimer->stop();
  QFrame::hideEvent(event);
  emit visibilityChanged(false);
}
