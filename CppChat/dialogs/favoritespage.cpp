#include "favoritespage.h"
#include "ui_favoritespage.h"
#include <QScrollBar>

FavoritesPage::FavoritesPage(QWidget *parent) : QWidget(parent), ui(new Ui::FavoritesPage)
{
  ui->setupUi(this);

  // 这是静态占位页：按钮保持可见但不触发收藏业务。
  for (auto *button :
       {ui->write_note_btn, ui->favorite_all_btn, ui->favorite_recent_btn, ui->favorite_link_btn,
        ui->favorite_media_btn, ui->favorite_message_btn, ui->favorite_audio_btn}) {
    button->setCursor(Qt::PointingHandCursor);
  }
  Reset();
}

FavoritesPage::~FavoritesPage()
{
  delete ui;
}

void FavoritesPage::Reset()
{
  ui->favorites_search_edit->clear();
  for (auto *button : {ui->favorite_all_btn, ui->favorite_recent_btn, ui->favorite_link_btn,
                       ui->favorite_media_btn, ui->favorite_message_btn, ui->favorite_audio_btn})
    button->setChecked(button == ui->favorite_all_btn);
  ui->favorites_scroll_area->verticalScrollBar()->setValue(0);
}
