#include "discoverpage.h"
#include "ui_discoverpage.h"

#include <QAbstractButton>
#include <QButtonGroup>

DiscoverPage::DiscoverPage(QWidget *parent) : QWidget(parent), ui(new Ui::DiscoverPage)
{
  ui->setupUi(this);
  setAttribute(Qt::WA_StyledBackground);
  ui->discover_menu->setAttribute(Qt::WA_StyledBackground);
  ui->discover_content->setAttribute(Qt::WA_StyledBackground);

  // 所有控件和按钮组都在 .ui 中定义；这里仅连接占位交互。
  for (auto *button : ui->discover_buttons->buttons()) {
    connect(button, &QAbstractButton::clicked, this, [this, button] {
      ui->placeholder_title->setText(button->text());
      ui->placeholder_title->show();
      ui->placeholder_description->show();
    });
  }
  Reset();
}

DiscoverPage::~DiscoverPage()
{
  delete ui;
}

void DiscoverPage::Reset()
{
  ui->discover_buttons->setExclusive(false);
  for (auto *button : ui->discover_buttons->buttons()) {
    button->setChecked(false);
  }
  ui->discover_buttons->setExclusive(true);
  ui->placeholder_title->clear();
  ui->placeholder_title->hide();
  ui->placeholder_description->hide();
}
