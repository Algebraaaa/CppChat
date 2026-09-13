#ifndef SEARCHLIST_H
#define SEARCHLIST_H
#include "common/userdata.h"
#include <QListWidget>
#include <QPointer>
class QDialog;
class QLineEdit;
class LoadingDlg;
class SearchList : public QListWidget {
  Q_OBJECT
public:
  explicit SearchList(QWidget *parent = nullptr);
  void CloseFindDlg();
  void SetSearchEdit(QWidget *edit);
signals:
  void sig_jump_chat_item(std::shared_ptr<SearchInfo> info);
private:
  void waitPending(bool pending);
  void slot_item_clicked(QListWidgetItem *item);
  void slot_user_search(std::shared_ptr<SearchInfo> info);
  QPointer<QDialog> _findDialog;
  QPointer<LoadingDlg> _loadingDialog;
  QLineEdit *_searchEdit = nullptr;
  bool _sendPending = false;
};
#endif
