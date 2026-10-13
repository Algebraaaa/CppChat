#ifndef PROFILEDIALOG_H
#define PROFILEDIALOG_H
#include <QDialog>
namespace Ui {
class ProfileDialog;
}

// 个人资料是全局工具窗口，不占用四个业务模块的导航状态。
class ProfileDialog : public QDialog {
  Q_OBJECT
public:
  explicit ProfileDialog(QWidget *parent = nullptr);
  ~ProfileDialog() override;
  void Refresh();
  void Reset();
signals:
  void sig_profile_changed();
  void sig_logout();

private:
  Ui::ProfileDialog *ui;
};
#endif
