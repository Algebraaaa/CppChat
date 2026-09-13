#include "searchlist.h"
#include "dialogs/adduseritem.h"
#include "dialogs/findfaildlg.h"
#include "dialogs/findsuccessdlg.h"
#include "dialogs/loadingdlg.h"
#include "network/tcpmgr.h"
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>

SearchList::SearchList(QWidget *parent) : QListWidget(parent)
{
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *spacer = new QListWidgetItem(this);
  spacer->setSizeHint(QSize(250, 10));
  spacer->setFlags(Qt::NoItemFlags);
  auto *widget = new AddUserItem;
  widget->setAttribute(Qt::WA_TransparentForMouseEvents);
  auto *item = new QListWidgetItem(this);
  item->setSizeHint(widget->sizeHint());
  setItemWidget(item, widget);
  connect(this, &QListWidget::itemClicked, this, &SearchList::slot_item_clicked);
  auto tcp = TcpMgr::GetInstance();
  connect(tcp.get(), &TcpMgr::sig_user_search, this, &SearchList::slot_user_search);
  connect(tcp.get(), &TcpMgr::sig_request_failed, this, [this](ReqId request, const QString &message) {
    if (request != ID_SEARCH_USER_REQ || !_sendPending) return;
    waitPending(false);
    QMessageBox::warning(this, tr("查找失败"), message);
  });
  connect(tcp.get(), &TcpMgr::sig_connection_closed, this, &SearchList::CloseFindDlg);
  connect(tcp.get(), &TcpMgr::sig_notify_offline, this, &SearchList::CloseFindDlg);
}
void SearchList::SetSearchEdit(QWidget *edit) { _searchEdit = qobject_cast<QLineEdit *>(edit); }
void SearchList::slot_item_clicked(QListWidgetItem *item)
{
  if (_sendPending || !_searchEdit || !qobject_cast<AddUserItem *>(itemWidget(item))) return;
  const QString query = _searchEdit->text().trimmed();
  if (query.isEmpty()) { _searchEdit->setFocus(); return; }
  CloseFindDlg();
  waitPending(true);
  emit TcpMgr::GetInstance()->sig_send_data(ID_SEARCH_USER_REQ,
      QJsonDocument(QJsonObject{{"uid", query}}).toJson(QJsonDocument::Compact));
}
void SearchList::slot_user_search(std::shared_ptr<SearchInfo> info)
{
  if (!_sendPending) return;
  waitPending(false);
  if (!info) {
    _findDialog = new FindFailDlg(this);
  } else {
    auto *dialog = new FindSuccessDlg(this);
    dialog->SetSearchInfo(info);
    connect(dialog, &FindSuccessDlg::sig_jump_chat_item, this, &SearchList::sig_jump_chat_item);
    _findDialog = dialog;
  }
  _findDialog->setAttribute(Qt::WA_DeleteOnClose);
  _findDialog->open();
}
void SearchList::CloseFindDlg()
{
  waitPending(false);
  if (_findDialog) _findDialog->close();
  _findDialog.clear();
}
void SearchList::waitPending(bool pending)
{
  _sendPending = pending;
  if (!pending) {
    if (_loadingDialog) {
      _loadingDialog->hide();
      _loadingDialog->deleteLater();
      _loadingDialog.clear();
    }
    return;
  }
  if (!_loadingDialog) {
    _loadingDialog = new LoadingDlg(this);
    _loadingDialog->setModal(false);
  }
  _loadingDialog->show();
}
