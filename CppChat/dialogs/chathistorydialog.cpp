#include "chathistorydialog.h"
#include "common/chatstore.h"
#include "network/resourcemgr.h"
#include "network/usermgr.h"
#include "ui_chathistorydialog.h"
#include "widgets/picturebubble.h"
#include "widgets/textbubble.h"
#include <QButtonGroup>
#include <QDate>
#include <QEvent>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

ChatHistoryDialog::ChatHistoryDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::ChatHistoryDialog), _categories(new QButtonGroup(this))
{
  ui->setupUi(this);
  setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                 Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);
  setModal(false);
  ui->search_icon->setPixmap(QIcon(":/icons/action-search.svg").pixmap(22, 22));
  ui->empty_icon->setPixmap(QIcon(":/icons/favorites-message.svg").pixmap(48, 48));
  ui->voice_search_btn->setEnabled(false);
  ui->voice_search_btn->setToolTip(tr("暂不支持语音搜索"));
  for (auto *button : {ui->files_btn, ui->music_btn, ui->miniapps_btn, ui->channels_btn}) {
    button->setEnabled(false);
    button->setToolTip(tr("当前协议尚不支持此类消息"));
  }
  for (auto *button : {ui->all_btn, ui->media_btn, ui->links_btn})
    _categories->addButton(button);
  connect(ui->all_btn, &QPushButton::clicked, this, [this] {
    _type = -1;
    Query();
  });
  connect(ui->media_btn, &QPushButton::clicked, this, [this] {
    _type = 1;
    Query();
  });
  connect(ui->links_btn, &QPushButton::clicked, this, [this] {
    _type = 3;
    Query();
  });
  connect(ui->date_btn, &QPushButton::clicked, this, [this] {
    bool accepted = false;
    const auto value =
        QInputDialog::getText(this, tr("按日期查找"), tr("输入 yyyy-MM-dd；留空显示全部日期"),
                              QLineEdit::Normal, _date, &accepted)
            .trimmed();
    if (!accepted)
      return;
    if (!value.isEmpty() &&
        (!QDate::fromString(value, "yyyy-MM-dd").isValid() ||
         QDate::fromString(value, "yyyy-MM-dd").toString("yyyy-MM-dd") != value)) {
      ui->date_btn->setChecked(!_date.isEmpty());
      QMessageBox::information(this, tr("日期格式错误"), tr("请输入有效日期，例如 2026-10-02。"));
      return;
    }
    _date = value;
    ui->date_btn->setChecked(!value.isEmpty());
    ui->date_btn->setText(value.isEmpty() ? tr("日期") : value);
    Query();
  });
  connect(ui->refresh_btn, &QPushButton::clicked, this, [this] { Query(); });
  connect(ui->previous_btn, &QPushButton::clicked, this, [this] {
    _pageIndex = qMax(0, _pageIndex - 1);
    Query(false);
  });
  connect(ui->next_btn, &QPushButton::clicked, this, [this] {
    _pageCursors.resize(_pageIndex + 1);
    _pageCursors.append(_nextCursor);
    ++_pageIndex;
    Query(false);
  });
  _searchTimer = new QTimer(this);
  _searchTimer->setSingleShot(true);
  _searchTimer->setInterval(250);
  connect(_searchTimer, &QTimer::timeout, this, [this] { Query(); });
  connect(ui->search_edit, &QLineEdit::textChanged, this, [this] {
    ++_revision;
    ui->next_btn->setEnabled(false);
    ui->previous_btn->setEnabled(false);
    _searchTimer->start();
  });
  connect(ui->search_edit, &QLineEdit::returnPressed, this, [this] { Query(); });
  Reset();
}

ChatHistoryDialog::~ChatHistoryDialog() { delete ui; }

void ChatHistoryDialog::SetConversation(int threadId, const QString &name)
{
  if (_threadId != threadId)
    Reset();
  _threadId = threadId;
  setWindowTitle(tr("与“%1”的聊天记录").arg(name));
  Query();
}

void ChatHistoryDialog::Reset()
{
  hide();
  ++_revision;
  _threadId = _pageIndex = 0;
  _pageCursors = {HistoryCursor{}};
  _type = -1;
  _date.clear();
  for (auto *button : _categories->buttons())
    button->setChecked(button == ui->all_btn);
  ui->date_btn->setChecked(false);
  ui->date_btn->setText(tr("日期"));
  ui->search_edit->clear();
  _searchTimer->stop();
  ui->results_list->clear();
  ui->results_list->hide();
  ui->previous_btn->setEnabled(false);
  ui->next_btn->setEnabled(false);
  ui->records_scroll->show();
  ui->empty_title->setText(tr("本机聊天记录"));
  ui->empty_description->setText(tr("登录后自动补齐记录，可按文字、图片、链接或日期查找。"));
  setWindowTitle(tr("聊天记录"));
}

void ChatHistoryDialog::Query(bool reset)
{
  _searchTimer->stop();
  if (_threadId <= 0)
    return;
  if (reset) {
    _pageIndex = 0;
    _pageCursors = {HistoryCursor{}};
  }
  const auto revision = ++_revision;
  const QPointer<ChatHistoryDialog> guard(this);
  ui->next_btn->setEnabled(false);
  ui->previous_btn->setEnabled(false);
  ChatStore::instance()->SearchMessages(
      _threadId, ui->search_edit->text().trimmed(), _type, _pageCursors[_pageIndex],
      [guard, revision](const ChatStoreResult &result) {
        if (!guard || guard->_revision != revision)
          return;
        guard->ui->results_list->clear();
        guard->ui->results_list->setVisible(!result.messages.isEmpty());
        guard->ui->records_scroll->setVisible(result.messages.isEmpty());
        guard->ui->empty_title->setText(result.error.isEmpty() ? guard->tr("没有找到记录")
                                                               : guard->tr("读取失败"));
        guard->ui->empty_description->setText(
            result.error.isEmpty()
                ? guard->tr("当前查询仅包含已同步到本机的记录；后台同步完成后重新查询。")
                : result.error);
        guard->_nextCursor = result.nextCursor;
        guard->ui->previous_btn->setEnabled(guard->_pageIndex > 0);
        guard->ui->next_btn->setEnabled(result.more);
        for (const auto &message : result.messages) {
          auto *row = new QWidget;
          row->installEventFilter(guard);
          auto *layout = new QVBoxLayout(row);
          const auto friendInfo = UserMgr::GetInstance()->GetFriendById(message.senderUid);
          const auto name =
              message.senderUid == UserMgr::GetInstance()->GetUid() ? guard->tr("我")
              : friendInfo ? (friendInfo->_back.isEmpty() ? friendInfo->_name : friendInfo->_back)
                           : QString::number(message.senderUid);
          auto *label = new QLabel(name + "  " + message.time +
                                       (message.serverId == 0 ? guard->tr("  待确认") : QString()),
                                   row);
          label->setTextFormat(Qt::PlainText);
          layout->addWidget(label);
          if (message.type == 1) {
            auto *picture = new PictureBubble(ChatRole::Other);
            layout->addWidget(picture);
            const auto fileId = message.content;
            picture->loadImage(fileId);
          } else
            layout->addWidget(new TextBubble(ChatRole::Other, message.content));
          auto *item = new QListWidgetItem(guard->ui->results_list);
          item->setSizeHint(row->sizeHint().expandedTo(QSize(320, message.type == 1 ? 220 : 86)));
          guard->ui->results_list->setItemWidget(item, row);
        }
      },
      _date);
}

bool ChatHistoryDialog::eventFilter(QObject *watched, QEvent *event)
{
  if (event->type() == QEvent::LayoutRequest && ui->results_list) {
    for (int i = 0; i < ui->results_list->count(); ++i) {
      auto *item = ui->results_list->item(i);
      auto *row = ui->results_list->itemWidget(item);
      if (row != watched)
        continue;
      const auto size = row->sizeHint().expandedTo(QSize(320, 86));
      if (item->sizeHint() != size)
        item->setSizeHint(size);
      break;
    }
  }
  return QDialog::eventFilter(watched, event);
}
