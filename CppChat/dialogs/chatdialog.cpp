#include "chatdialog.h"
#include "chatuserwid.h"
#include "loadingdlg.h"
#include "ui_chatdialog.h"
#include <array>

namespace {

struct ChatUserTestData {
  QString displayName;
  QString avatarResource;
  QString lastMessage;
};

// 聊天主页的固定测试数据。
// 每条记录将用户名、头像和最后一条消息绑定在一起，确保每次启动的测试结果一致。
const std::array<ChatUserTestData, 33> kChatUserTestData = {{
  { QStringLiteral("林夏"), QStringLiteral(":/res/head_1.jpg"),
    QStringLiteral("今晚一起调试客户端吗？") },
  { QStringLiteral("周屿"), QStringLiteral(":/res/head_2.jpg"),
    QStringLiteral("需求文档已经更新了。") },
  { QStringLiteral("Alex"), QStringLiteral(":/res/head_3.jpg"),
    QStringLiteral("The latest build is ready.") },
  { QStringLiteral("Mia"), QStringLiteral(":/res/head_4.jpg"),
    QStringLiteral("明天下午三点开会。") },
  { QStringLiteral("Chen"), QStringLiteral(":/res/head_5.jpg"),
    QStringLiteral("接口联调通过了。") },
  { QStringLiteral("Aurora"), QStringLiteral(":/res/head_6.jpg"),
    QStringLiteral("欢迎来到 CppChat！") },
  { QStringLiteral("Nova"), QStringLiteral(":/res/head_7.jpg"),
    QStringLiteral("头像资源加载正常。") },
  { QStringLiteral("Willow"), QStringLiteral(":/res/head_8.jpg"),
    QStringLiteral("搜索功能很好用。") },
  { QStringLiteral("Felix"), QStringLiteral(":/res/head_9.jpg"),
    QStringLiteral("稍后把日志发给你。") },
  { QStringLiteral("Leo"), QStringLiteral(":/res/head_10.jpg"),
    QStringLiteral("今天的任务完成啦。") },
  { QStringLiteral("Rust Team"), QStringLiteral(":/res/head_6.jpg"),
    QStringLiteral("消息列表滚动测试。") },
  { QStringLiteral("Qt Group"), QStringLiteral(":/res/head_7.jpg"),
    QStringLiteral("QSS 样式已经更新。") },
  { QStringLiteral("Backend"), QStringLiteral(":/res/head_8.jpg"),
    QStringLiteral("ChatServer is online.") },
  { QStringLiteral("Luna"), QStringLiteral(":/res/head_11.jpg"),
    QStringLiteral("今晚的月色真好。") },
  { QStringLiteral("Iris"), QStringLiteral(":/res/head_12.jpg"),
    QStringLiteral("新的交互稿已经发你了。") },
  { QStringLiteral("Owen"), QStringLiteral(":/res/head_13.jpg"),
    QStringLiteral("我正在检查消息协议。") },
  { QStringLiteral("Hazel"), QStringLiteral(":/res/head_14.jpg"),
    QStringLiteral("周末一起喝咖啡吗？") },
  { QStringLiteral("Milo"), QStringLiteral(":/res/head_15.jpg"),
    QStringLiteral("客户端启动速度不错。") },
  { QStringLiteral("Ruby"), QStringLiteral(":/res/head_16.jpg"),
    QStringLiteral("测试用例已经补齐。") },
  { QStringLiteral("Theo"), QStringLiteral(":/res/head_17.jpg"),
    QStringLiteral("TCP 连接保持正常。") },
  { QStringLiteral("Chloe"), QStringLiteral(":/res/head_18.jpg"),
    QStringLiteral("收到，稍后回复你。") },
  { QStringLiteral("Jasper"), QStringLiteral(":/res/head_19.jpg"),
    QStringLiteral("代码审查已经完成。") },
  { QStringLiteral("Zoe"), QStringLiteral(":/res/head_20.jpg"),
    QStringLiteral("明天见，晚安！") },
  { QStringLiteral("Akari"), QStringLiteral(":/res/head_21.jpg"),
    QStringLiteral("新番更新啦，一起看吗？") },
  { QStringLiteral("Hikari"), QStringLiteral(":/res/head_22.jpg"),
    QStringLiteral("今天也要元气满满！") },
  { QStringLiteral("Ren"), QStringLiteral(":/res/head_23.jpg"),
    QStringLiteral("这个表情包太可爱了。") },
  { QStringLiteral("Sora"), QStringLiteral(":/res/head_24.jpg"),
    QStringLiteral("组队任务还差一个人。") },
  { QStringLiteral("Yuki"), QStringLiteral(":/res/head_25.jpg"),
    QStringLiteral("漫画看到最新一话了吗？") },
  { QStringLiteral("Rin"), QStringLiteral(":/res/head_26.jpg"),
    QStringLiteral("头像切换测试通过。") },
  { QStringLiteral("Kaito"), QStringLiteral(":/res/head_27.jpg"),
    QStringLiteral("晚上八点准时上线。") },
  { QStringLiteral("Mei"), QStringLiteral(":/res/head_28.jpg"),
    QStringLiteral("今天画了一张新立绘。") },
  { QStringLiteral("Haru"), QStringLiteral(":/res/head_29.jpg"),
    QStringLiteral("活动奖励已经领取。") },
  { QStringLiteral("Aoi"), QStringLiteral(":/res/head_30.jpg"),
    QStringLiteral("下次漫展见！") },
}};

} // namespace

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
