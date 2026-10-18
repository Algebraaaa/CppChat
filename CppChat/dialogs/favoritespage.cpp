#include "favoritespage.h"
#include "common/chatstore.h"
#include "common/imageloader.h"
#include "ui_favoritespage.h"
#include "widgets/audiobubble.h"
#include <QButtonGroup>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

FavoritesPage::FavoritesPage(QWidget *parent) : QWidget(parent), ui(new Ui::FavoritesPage)
{
  ui->setupUi(this);

  ui->write_note_btn->setEnabled(false);
  ui->write_note_btn->setToolTip(tr("当前支持收藏聊天消息，独立笔记尚未开放"));
  ui->favorite_recent_btn->setToolTip(tr("最近 7 天收藏的消息"));
  auto *group = new QButtonGroup(this);
  const QList<QPair<QPushButton *, FavoriteCategory>> filters{
      {ui->favorite_all_btn, FavoriteCategory::All},
      {ui->favorite_recent_btn, FavoriteCategory::Recent},
      {ui->favorite_link_btn, FavoriteCategory::Links},
      {ui->favorite_media_btn, FavoriteCategory::Images},
      {ui->favorite_message_btn, FavoriteCategory::Messages},
      {ui->favorite_audio_btn, FavoriteCategory::Audio}};
  for (const auto &entry : filters) {
    group->addButton(entry.first);
    connect(entry.first, &QPushButton::clicked, this, [this, entry] {
      _category = entry.second;
      ui->favorites_title_lb->setText(entry.first->text().trimmed());
      Load();
    });
  }
  _search.setSingleShot(true);
  _search.setInterval(200);
  connect(ui->favorites_search_edit, &QLineEdit::textChanged, &_search,
          qOverload<>(&QTimer::start));
  connect(&_search, &QTimer::timeout, this, [this] { Load(); });
  auto *content = new QWidget(this);
  auto *layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  _status = new QLabel(content);
  _status->setObjectName("favoritesStatus");
  _status->setTextFormat(Qt::PlainText);
  _status->setWordWrap(true);
  layout->addWidget(_status);
  _list = new QListWidget(content);
  _list->setObjectName("favoritesList");
  _list->setWordWrap(true);
  _list->setUniformItemSizes(true);
  layout->addWidget(_list, 1);
  _more = new QPushButton(tr("加载更多"), content);
  _more->setObjectName("favoritesLoadMore");
  layout->addWidget(_more);
  delete ui->favorites_scroll_area->takeWidget();
  ui->favorites_scroll_area->setWidget(content);
  _open = new QPushButton(tr("查看"), this);
  _send = new QPushButton(tr("发送给…"), this);
  _remove = new QPushButton(tr("取消收藏"), this);
  _open->setObjectName("favoriteOpen");
  _send->setObjectName("favoriteSend");
  _remove->setObjectName("favoriteRemove");
  for (auto *button : {_open, _send, _remove})
    ui->header_layout->addWidget(button);
  connect(_open, &QPushButton::clicked, this, [this] { Open(false); });
  connect(_send, &QPushButton::clicked, this, [this] { Open(true); });
  connect(_remove, &QPushButton::clicked, this, &FavoritesPage::Remove);
  connect(_more, &QPushButton::clicked, this, [this] { Load(true); });
  connect(_list, &QListWidget::itemActivated, this, [this] { Open(false); });
  connect(_list, &QListWidget::itemSelectionChanged, this, &FavoritesPage::UpdateControls);
  connect(ChatStore::instance(), &ChatStore::favoritesChanged, this, [this] {
    if (isVisible())
      Load();
  });
  connect(ChatStore::instance(), &ChatStore::accountChanged, this, &FavoritesPage::Reset);
  connect(ChatStore::instance(), &ChatStore::accountReady, this, [this] {
    if (isVisible())
      Load();
  });
  Reset();
}

FavoritesPage::~FavoritesPage() { delete ui; }

void FavoritesPage::Reset()
{
  ++_revision;
  _search.stop();
  if (_preview)
    _preview->close();
  _preview.clear();
  _list->clear();
  _cursor = 0;
  _busy = _hasMore = false;
  ui->favorites_search_edit->clear();
  _search.stop();
  _category = FavoriteCategory::All;
  ui->favorite_all_btn->setChecked(true);
  ui->favorites_title_lb->setText(tr("全部收藏"));
  _status->clear();
  UpdateControls();
}

void FavoritesPage::showEvent(QShowEvent *event)
{
  QWidget::showEvent(event);
  Load();
}

void FavoritesPage::UpdateControls()
{
  const bool selected = !_busy && _list->currentItem();
  for (auto *button : {_open, _send, _remove})
    button->setEnabled(selected);
  _more->setVisible(_hasMore);
  _more->setEnabled(!_busy);
}

void FavoritesPage::Load(bool append)
{
  if (append && (_busy || !_hasMore))
    return;
  _search.stop();
  const auto revision = ++_revision;
  if (!append) {
    _list->clear();
    _cursor = 0;
  }
  if (!ChatStore::instance()->IsReady()) {
    _busy = _hasMore = false;
    _status->setText(tr("请登录并等待本机记录加载完成"));
    UpdateControls();
    return;
  }
  _busy = true;
  _status->setText(tr("正在读取本机收藏…"));
  UpdateControls();
  const QPointer<FavoritesPage> guard(this);
  ChatStore::instance()->LoadFavorites(
      ui->favorites_search_edit->text(), _category, _cursor,
      [guard, revision](const ChatStoreResult &result) {
        if (!guard || guard->_revision != revision)
          return;
        guard->_busy = false;
        guard->_hasMore = result.error.isEmpty() && result.more;
        for (const auto &value : result.favorites) {
          const QString title = value.type == 1   ? guard->tr("[图片]")
                                : value.type == 3 ? guard->tr("[语音]")
                                                  : value.content.left(250);
          const QString source =
              value.senderName + " · " +
              QDateTime::fromMSecsSinceEpoch(value.createdMs).toString("yyyy-MM-dd HH:mm");
          auto *item = new QListWidgetItem(title + '\n' + source, guard->_list);
          item->setData(Qt::UserRole, value.id);
          item->setToolTip(title);
          item->setSizeHint(QSize(0, 110));
          guard->_cursor = value.id;
        }
        guard->_status->setText(!result.error.isEmpty() ? guard->tr("读取失败，请重新打开收藏重试")
                                : guard->_list->count() == 0
                                    ? guard->tr("没有符合条件的收藏；可在消息右键菜单中收藏")
                                    : guard->tr("已加载 %1 项本机收藏").arg(guard->_list->count()));
        guard->UpdateControls();
      });
}

void FavoritesPage::Open(bool resend)
{
  if (_busy || !_list->currentItem())
    return;
  const auto id = _list->currentItem()->data(Qt::UserRole).toLongLong();
  const auto revision = _revision;
  const QPointer<FavoritesPage> guard(this);
  ChatStore::instance()->GetFavorite(id, [guard, revision, resend](const ChatStoreResult &result) {
    if (!guard || guard->_revision != revision || !guard->isVisible())
      return;
    if (!result.error.isEmpty() || result.favorites.isEmpty()) {
      guard->_status->setText(guard->tr("无法读取该收藏，请刷新后重试"));
      return;
    }
    const auto value = result.favorites.first();
    if (resend) {
      emit guard->resendRequested({value});
      return;
    }
    if (guard->_preview)
      guard->_preview->close();
    auto *dialog = new QDialog(guard.data());
    dialog->setObjectName("favoritePreview");
    dialog->setProperty("groupDialog", true);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(guard->tr("收藏 · %1").arg(value.senderName));
    dialog->resize(680, 520);
    auto *layout = new QVBoxLayout(dialog);
    if (value.type == 0) {
      auto *text = new QPlainTextEdit(dialog);
      text->setReadOnly(true);
      text->setPlainText(value.content);
      layout->addWidget(text, 1);
    } else if (value.type == 3) {
      auto *audio = new AudioBubble(ChatRole::Other, dialog);
      audio->SetSnapshot(value.media);
      layout->addWidget(audio);
    } else {
      auto *image = new QLabel(guard->tr("正在读取图片…"), dialog);
      image->setAlignment(Qt::AlignCenter);
      layout->addWidget(image, 1);
      ImageLoader::instance()->load(
          "favorite:" + QString::number(ChatStore::instance()->Generation()) + ':' +
              QString::number(value.id),
          value.media, QSize(620, 420), 1.0, dialog, [image](const LoadedImage &loaded) {
            if (loaded.image.isNull())
              image->setText(QObject::tr("图片无法读取"));
            else
              image->setPixmap(QPixmap::fromImage(loaded.image));
          });
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);
    guard->_preview = dialog;
    dialog->show();
  });
}

void FavoritesPage::Remove()
{
  if (_busy || !_list->currentItem())
    return;
  const auto id = _list->currentItem()->data(Qt::UserRole).toLongLong();
  const auto generation = ChatStore::instance()->Generation();
  if (QMessageBox::question(this, tr("取消收藏"), tr("取消这项本机收藏？原聊天记录不会被删除。"),
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No) != QMessageBox::Yes ||
      ChatStore::instance()->Generation() != generation)
    return;
  const QPointer<FavoritesPage> guard(this);
  ChatStore::instance()->RemoveFavorites({id}, [guard](const ChatStoreResult &result) {
    if (guard && !result.error.isEmpty())
      guard->_status->setText(guard->tr("取消收藏失败，内容仍然保留"));
  });
}
