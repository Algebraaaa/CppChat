#include "findsuccessdlg.h"
#include "ui_findsuccessdlg.h"
#include <QDir>
FindSuccessDlg::FindSuccessDlg(QWidget *parent) : QDialog(parent), ui(new Ui::FindSuccessDlg)
{
  ui->setupUi(this);
  // 设置对话框标题
  setWindowTitle("添加");
  // 隐藏对话框标题栏
  setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
  // 获取当前应用程序的路径
  QDir appDir(QCoreApplication::applicationDirPath());

  // 已存在时也会返回 true；不存在时创建目录。
  if (!appDir.mkpath("static")) {
    qWarning() << "创建头像目录失败";
  }

  QString pixPath = appDir.filePath("static/head_1.jpg");
  QPixmap headPix(pixPath);

  // 创建文件夹不会自动生成图片，加载失败时使用默认头像。
  if (headPix.isNull()) {
    headPix.load(":/res/head_1.jpg");
  }

  headPix = headPix.scaled(ui->head_lb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  ui->head_lb->setPixmap(headPix);
  ui->add_friend_btn->SetState("normal", "hover", "press");
  this->setModal(true);
}

FindSuccessDlg::~FindSuccessDlg()
{
  qDebug() << "FindSuccessDlg destruct";
  delete ui;
}

void FindSuccessDlg::SetSearchInfo(std::shared_ptr<SearchInfo> si)
{
  ui->name_lb->setText(si->_name);
  _si = si;
}

void FindSuccessDlg::on_add_friend_btn_clicked()
{
  // todo... 添加好友界面弹出
}
