#include "loadingdlg.h"
#include "ui_loadingdlg.h"
#include <QByteArray>
#include <QMovie>
LoadingDlg::LoadingDlg(QWidget *parent) : QDialog(parent), ui(new Ui::LoadingDlg)
{
  ui->setupUi(this);
  setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint
                 | Qt::WindowStaysOnTopHint);
  setAttribute(Qt::WA_TranslucentBackground);

  // parent 可能为空；只有存在父窗口时，加载层才跟随父窗口尺寸。
  if (parent != nullptr) {
    setFixedSize(parent->size());
  }

  // 将 this 设为父对象，LoadingDlg 销毁时 QMovie 会由 Qt 自动释放。
  QMovie *movie = new QMovie(":/res/loading.gif", QByteArray(), this);
  ui->loading_lb->setMovie(movie);
  movie->start();
}

LoadingDlg::~LoadingDlg()
{
  delete ui;
}
