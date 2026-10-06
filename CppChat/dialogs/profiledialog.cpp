#include "profiledialog.h"
#include "ui_profiledialog.h"

ProfileDialog::ProfileDialog(QWidget *parent) : QDialog(parent), ui(new Ui::ProfileDialog)
{
  ui->setupUi(this);
  setWindowModality(Qt::WindowModal);
  connect(ui->user_info_page, &UserInfoPage::sig_profile_changed, this,
          &ProfileDialog::sig_profile_changed);
  connect(ui->user_info_page, &UserInfoPage::sig_logout, this, [this] {
    reject();
    emit sig_logout();
  });
}
ProfileDialog::~ProfileDialog()
{
  delete ui;
}
void ProfileDialog::Refresh()
{
  ui->user_info_page->Refresh();
}
void ProfileDialog::Reset()
{
  ui->user_info_page->Reset();
}
