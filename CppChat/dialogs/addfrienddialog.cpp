#include "addfrienddialog.h"

#include "app/mainwindow.h"
#include "dialogs/findfaildlg.h"
#include "dialogs/findsuccessdlg.h"
#include "dialogs/loadingdlg.h"
#include "network/tcpmgr.h"
#include "ui_addfrienddialog.h"

#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>

AddFriendDialog::AddFriendDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::AddFriendDialog)
{
  ui->setupUi(this);
  setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);

  // 外观和布局由 .ui 定义；仅根据主窗口同步登录、注册的实际总尺寸。
  auto *mainWindow = parent ? qobject_cast<MainWindow *>(parent->window()) : nullptr;
  setFixedSize(mainWindow ? mainWindow->authenticationWindowSize() : size());
  ui->add_friend_title->setAttribute(Qt::WA_TransparentForMouseEvents);
  ui->add_friend_search_edit->SetMaxLength(27);
  ui->add_friend_search_edit->addAction(QIcon(":/icons/action-search.svg"),
                                      QLineEdit::LeadingPosition);
  if (parent)
    move(parent->window()->frameGeometry().center() - rect().center());

  connect(ui->add_friend_close_btn, &QPushButton::clicked, this, &QDialog::reject);
  connect(ui->add_friend_search_btn, &QPushButton::clicked, this, &AddFriendDialog::searchUser);
  connect(ui->add_friend_search_edit, &QLineEdit::returnPressed, this,
          &AddFriendDialog::searchUser);
  connect(ui->add_friend_search_edit, &QLineEdit::textChanged, this,
          [this](const QString &text) {
            ui->add_friend_search_btn->setEnabled(!_sendPending && !text.trimmed().isEmpty());
          });

  const auto tcp = TcpMgr::GetInstance();
  connect(tcp.get(), &TcpMgr::sig_user_search, this,
          [this](const std::shared_ptr<SearchInfo> &info) {
            if (_sendPending)
              handleSearchResult(info);
          });
  connect(tcp.get(), &TcpMgr::sig_request_failed, this,
          [this](ReqId request, const QString &message) {
            if (request != ID_SEARCH_USER_REQ || !_sendPending)
              return;
            setPending(false);
            QMessageBox::warning(this, tr("查找失败"), message);
          });
  connect(tcp.get(), &TcpMgr::sig_connection_closed, this, [this]() {
    if (_sendPending)
      setPending(false);
  });
  connect(tcp.get(), &TcpMgr::sig_notify_offline, this, [this]() {
    if (_sendPending)
      setPending(false);
  });
}

AddFriendDialog::~AddFriendDialog()
{
  if (_sendPending)
    TcpMgr::GetInstance()->CancelRequest(ID_SEARCH_USER_REQ);
  delete ui;
}

void AddFriendDialog::reject()
{
  if (_sendPending)
    TcpMgr::GetInstance()->CancelRequest(ID_SEARCH_USER_REQ);
  setPending(false);
  closeResultDialog();
  QDialog::reject();
}

void AddFriendDialog::searchUser()
{
  if (_sendPending)
    return;

  const QString query = ui->add_friend_search_edit->text().trimmed();
  if (query.isEmpty()) {
    ui->add_friend_search_edit->setFocus();
    return;
  }

  if (!TcpMgr::GetInstance()->IsConnected()) {
    QMessageBox::warning(this, tr("无法查找"), tr("当前未连接到服务器，请重新登录后再试。"));
    return;
  }

  closeResultDialog();
  setPending(true);
  emit TcpMgr::GetInstance()->sig_send_data(
      ID_SEARCH_USER_REQ,
      QJsonDocument(QJsonObject{{"uid", query}}).toJson(QJsonDocument::Compact));
}

void AddFriendDialog::handleSearchResult(const std::shared_ptr<SearchInfo> &info)
{
  setPending(false);
  closeResultDialog();

  if (!info) {
    _resultDialog = new FindFailDlg(this);
  } else {
    auto *dialog = new FindSuccessDlg(this);
    dialog->SetSearchInfo(info);
    connect(dialog, &FindSuccessDlg::sig_jump_chat_item, this,
            [this](const std::shared_ptr<SearchInfo> &user) {
              emit sig_jump_chat_item(user);
              accept();
            });
    _resultDialog = dialog;
  }

  _resultDialog->setAttribute(Qt::WA_DeleteOnClose);
  _resultDialog->open();
}

void AddFriendDialog::setPending(bool pending)
{
  _sendPending = pending;
  ui->add_friend_search_edit->setEnabled(!pending);
  ui->add_friend_search_btn->setEnabled(
      !pending && !ui->add_friend_search_edit->text().trimmed().isEmpty());

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

void AddFriendDialog::closeResultDialog()
{
  if (_resultDialog)
    _resultDialog->close();
  _resultDialog.clear();
}
