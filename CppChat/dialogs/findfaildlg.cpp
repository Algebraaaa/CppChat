#include "findfaildlg.h"
#include "ui_findfaildlg.h"
#include <QDebug>
FindFailDlg::FindFailDlg(QWidget *parent) : QDialog(parent), ui(new Ui::FindFailDlg)
{
  ui->setupUi(this);
  setWindowTitle("添加");
  setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
  this->setObjectName("FindFailDlg");
  ui->fail_sure_btn->SetState("normal", "hover", "pressed");
  this->setModal(true);
}

FindFailDlg::~FindFailDlg()
{
  qDebug() << "Find FailDlg destruct";
  delete ui;
}

void FindFailDlg::on_fail_sure_btn_clicked()
{
  // 结束模态对话框，恢复主窗口交互。
  accept();
}
