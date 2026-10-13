#ifndef ADDFRIENDDIALOG_H
#define ADDFRIENDDIALOG_H

#include "common/userdata.h"
#include <QDialog>
#include <QPointer>

class LoadingDlg;
namespace Ui {
class AddFriendDialog;
}

// “更多”菜单中的添加朋友窗口。
// 它只负责向服务器查找陌生用户，顶部主搜索框则只做本地搜索。
class AddFriendDialog : public QDialog {
  Q_OBJECT

public:
  explicit AddFriendDialog(QWidget *parent = nullptr);
  ~AddFriendDialog() override;

signals:
  void sig_jump_chat_item(std::shared_ptr<SearchInfo> info);

protected:
  void reject() override;

private:
  void searchUser();
  void handleSearchResult(const std::shared_ptr<SearchInfo> &info);
  void setPending(bool pending);
  void closeResultDialog();

  Ui::AddFriendDialog *ui;
  QPointer<QDialog> _resultDialog;
  QPointer<LoadingDlg> _loadingDialog;
  bool _sendPending = false;
};

#endif // ADDFRIENDDIALOG_H
