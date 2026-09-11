#include "chatdialog.h"
#include "chatuserwid.h"
#include "loadingdlg.h"
#include "ui_chatdialog.h"

ChatDialog::ChatDialog(QWidget *parent)
  : QDialog(parent), ui(new Ui::ChatDialog), _mode(ChatUIMode::ChatMode),
    _state(ChatUIMode::ChatMode), _b_loading(false)
{
  ui->setupUi(this);
  ui->add_btn->SetState("normal", "hover", "press");
  QAction *searchAction = new QAction(ui->search_edit);
  searchAction->setIcon(QIcon(":/res/search.png"));
  ui->search_edit->addAction(searchAction, QLineEdit::LeadingPosition);
  ui->search_edit->setPlaceholderText(QStringLiteral("搜索"));

  // 创建一个清除动作并设置图标
  QAction *clearAction = new QAction(ui->search_edit);
  clearAction->setIcon(QIcon(":/res/close_transparent.png"));
  // 初始时不显示清除图标
  // 将清除动作添加到LineEdit的末尾位置
  ui->search_edit->addAction(clearAction, QLineEdit::TrailingPosition);

  // 当需要显示清除图标时，更改为实际的清除图标
  connect(ui->search_edit, &QLineEdit::textChanged, [clearAction](const QString &text) {
    if (!text.isEmpty()) {
      clearAction->setIcon(QIcon(":/res/close_search.png"));
    } else {
      clearAction->setIcon(QIcon(":/res/close_transparent.png")); // 文本为空时，切换回透明图标
    }
  });

  // 连接清除动作的触发信号到槽函数，用于清除文本
  connect(clearAction, &QAction::triggered, [this, clearAction]() {
    ui->search_edit->clear();
    clearAction->setIcon(QIcon(":/res/close_transparent.png")); // 清除文本后，切换回透明图标
    ui->search_edit->clearFocus();
    // 清除按钮被按下则不显示搜索框
    ShowSearch(false);
  });
  ShowSearch(false);
  // 限制用户最多输入 27 个完整可见字符，而不是 27 个 UTF-8 字节。
  ui->search_edit->SetMaxLength(27);
  connect(ui->chat_user_list, &ChatUserList::sig_loading_chat_user, this,
          &ChatDialog::slot_loading_chat_user);
  addChatUserList();

  QPixmap pixmap(":/res/head_1.jpg");
  ui->side_head_lb->setPixmap(pixmap); // 将图片设置到QLabel上
  QPixmap scaledPixmap
    = pixmap.scaled(ui->side_head_lb->size(), Qt::KeepAspectRatio); // 将图片缩放到label的大小
  ui->side_head_lb->setPixmap(scaledPixmap);                        // 将缩放后的图片设置到QLabel上
  ui->side_head_lb->setScaledContents(true); // 设置QLabel自动缩放图片内容以适应大小

  ui->side_chat_lb->setProperty("state", "normal");

  ui->side_chat_lb->SetState("normal", "hover", "pressed", "selected_normal", "selected_hover",
                             "selected_pressed");

  ui->side_contact_lb->SetState("normal", "hover", "pressed", "selected_normal", "selected_hover",
                                "selected_pressed");

  AddLBGroup(ui->side_chat_lb);
  AddLBGroup(ui->side_contact_lb);

  connect(ui->side_chat_lb, &StateWidget::clicked, this, [this]() {
    // 当前位于聊天界面：聊天图标保持绿色选中，其他导航图标恢复普通状态。
    ClearLabelState(ui->side_chat_lb);
    ui->side_chat_lb->SetSelected(true);
    ui->stackedWidget->setCurrentWidget(ui->chat_page);
    _state = ChatUIMode::ChatMode;
    ShowSearch(false);
  });
  connect(ui->side_contact_lb, &StateWidget::clicked, this, [this]() {
    // 当前位于联系人界面：联系人图标保持绿色选中，其他导航图标恢复普通状态。
    ClearLabelState(ui->side_contact_lb);
    ui->side_contact_lb->SetSelected(true);
    ui->stackedWidget->setCurrentWidget(ui->friend_apply_page);
    _state = ChatUIMode::ContactMode;
    ShowSearch(false);
  });
  // 链接搜索框输入变化
  connect(ui->search_edit, &QLineEdit::textChanged, this, [this](const QString &str) {
    if (!str.isEmpty()) {
      ShowSearch(true);
    }
  });
  this->installEventFilter(this);
  ui->side_chat_lb->SetSelected(true);
}

void ChatDialog::ClearLabelState(StateWidget *lb)
{
  for (auto &ele : _lb_list) {
    if (ele == lb) {
      continue;
    }

    ele->ClearState();
  }
}
void ChatDialog::AddLBGroup(StateWidget *lb)
{
  _lb_list.push_back(lb);
}

bool ChatDialog::eventFilter(QObject *watched, QEvent *event)
{
  if (event->type() == QEvent::MouseButtonPress) {
    QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
    handleGlobalMousePress(mouseEvent);
  }
  return QDialog::eventFilter(watched, event);
}

void ChatDialog::handleGlobalMousePress(QMouseEvent *event)
{
  // 实现点击位置的判断和处理逻辑
  // 先判断是否处于搜索模式，如果不处于搜索模式则直接返回
  if (_mode != ChatUIMode::SearchMode) {
    return;
  }

  // 将鼠标点击位置转换为搜索列表坐标系中的位置
  QPoint posInSearchList = ui->search_list->mapFromGlobal(event->globalPos());
  // 判断点击位置是否在聊天列表的范围内
  if (!ui->search_list->rect().contains(posInSearchList)) {
    // 如果不在聊天列表内，清空输入框
    ui->search_edit->clear();
    ShowSearch(false);
  }
}
void ChatDialog::ShowSearch(bool bsearch)
{
  if (bsearch) {
    ui->chat_user_list->hide();
    ui->con_user_list->hide();
    ui->search_list->show();
    _mode = ChatUIMode::SearchMode;
  } else if (_state == ChatUIMode::ChatMode) {
    ui->chat_user_list->show();
    ui->con_user_list->hide();
    ui->search_list->hide();
    _mode = ChatUIMode::ChatMode;
  } else if (_state == ChatUIMode::ContactMode) {
    ui->chat_user_list->hide();
    ui->search_list->hide();
    ui->con_user_list->show();
    _mode = ChatUIMode::ContactMode;
  }
}

void ChatDialog::slot_loading_chat_user()
{
  if (_b_loading) {
    return;
  }

  _b_loading = true;
  LoadingDlg *loadingDialog = new LoadingDlg(this);
  loadingDialog->setModal(true);
  loadingDialog->show();
  qDebug() << "add new data to list.....";
  addChatUserList();
  // 加载完成后关闭对话框
  loadingDialog->deleteLater();

  _b_loading = false;
}
ChatDialog::~ChatDialog()
{
  delete ui;
}

void ChatDialog::addChatUserList()
{
  for (const auto &testUser : kChatUserTestData) {
    auto *chatUserWidget = new ChatUserWid;
    chatUserWidget->SetInfo(testUser.displayName, testUser.avatarResource,
                            testUser.lastMessage);

    auto *item = new QListWidgetItem;
    item->setSizeHint(chatUserWidget->sizeHint());
    ui->chat_user_list->addItem(item);
    ui->chat_user_list->setItemWidget(item, chatUserWidget);
  }
}
