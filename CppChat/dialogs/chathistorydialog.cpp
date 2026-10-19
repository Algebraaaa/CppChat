#include "chathistorydialog.h"
#include "common/chatstore.h"
#include "network/resourcemgr.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/historyexporter.h"
#include "ui_chathistorydialog.h"
#include "widgets/audiobubble.h"
#include "widgets/picturebubble.h"
#include "widgets/textbubble.h"
#include <QButtonGroup>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QEvent>
#include <QFileDialog>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

ChatHistoryDialog::ChatHistoryDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::ChatHistoryDialog), _categories(new QButtonGroup(this))
{
  ui->setupUi(this);
  setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                 Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);
  setModal(false);
  resize(size().boundedTo(screen()->availableGeometry().size()));
  _scope = new QComboBox(this);
  _scope->setObjectName("historyConversationFilter");
  _scope->setAccessibleName(tr("聊天记录会话范围"));
  ui->window_layout->insertWidget(0, _scope);
  _scope->hide();
  _exporter = new HistoryExporter(this);
  _exportButton = new QPushButton(tr("导出记录"), this);
  _exportButton->setObjectName("exportHistoryButton");
  _exportButton->setAutoDefault(false);
  _exportButton->setToolTip(tr("按当前筛选导出全部页的已确认记录，最多 10000 条 / 32 MiB；文本和 "
                               "JSON 均为明文，不含附件文件"));
  ui->paging_layout->insertWidget(1, _exportButton);
  _exportStatus = new QLabel(this);
  _exportStatus->setObjectName("exportHistoryStatus");
  _exportStatus->setWordWrap(true);
  _exportStatus->setTextFormat(Qt::PlainText);
  ui->window_layout->insertWidget(2, _exportStatus);
  connect(_exportButton, &QPushButton::clicked, this, &ChatHistoryDialog::BeginExport);
  connect(_exporter, &HistoryExporter::busyChanged, this, &ChatHistoryDialog::SetExportBusy);
  connect(_exporter, &HistoryExporter::progress, this,
          [this](int count) { _exportStatus->setText(tr("正在导出：%1 条，可取消").arg(count)); });
  connect(_exporter, &HistoryExporter::finished, this, [this](const QString &, int count) {
    _exportStatus->setText(tr("已导出 %1 条已确认记录；文件为明文，请妥善保管").arg(count));
  });
  connect(_exporter, &HistoryExporter::failed, _exportStatus, &QLabel::setText);
  connect(_exporter, &HistoryExporter::cancelled, this,
          [this] { _exportStatus->setText(tr("已取消导出，原目标文件保留")); });
  connect(_scope, &QComboBox::currentIndexChanged, this, [this] {
    if (_global) {
      _threadId = _scope->currentData().toInt();
      Query();
    }
  });
  ui->search_icon->setPixmap(QIcon(":/icons/action-search.svg").pixmap(22, 22));
  ui->empty_icon->setPixmap(QIcon(":/icons/favorites-message.svg").pixmap(48, 48));
  ui->voice_search_btn->setEnabled(false);
  ui->voice_search_btn->setToolTip(tr("暂不支持语音搜索"));
  for (auto *button : {ui->files_btn, ui->miniapps_btn, ui->channels_btn}) {
    button->setEnabled(false);
    button->setToolTip(tr("当前协议尚不支持此类消息"));
  }
  ui->music_btn->setText(tr("语音"));
  ui->music_btn->setToolTip(tr("语音消息"));
  for (auto *button : {ui->all_btn, ui->media_btn, ui->links_btn, ui->music_btn})
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
  connect(ui->music_btn, &QPushButton::clicked, this, [this] {
    _type = 4;
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
    ui->results_list->clear();
    ui->next_btn->setEnabled(false);
    ui->previous_btn->setEnabled(false);
    _searchTimer->start();
  });
  connect(ui->search_edit, &QLineEdit::returnPressed, this, [this] { Query(); });
  connect(ChatStore::instance(), &ChatStore::localMessageDeleted, this, [this](int thread, int) {
    if ((_global || _threadId == thread) && isVisible())
      Query();
  });
  connect(ChatStore::instance(), &ChatStore::localMessagesDeleted, this,
          [this](int thread, const QVector<int> &) {
            if ((_global || _threadId == thread) && isVisible())
              Query();
          });
  connect(ChatStore::instance(), &ChatStore::localHistoryCleared, this, [this](int thread) {
    if ((_global || _threadId == thread) && isVisible())
      Query();
  });
  connect(ChatStore::instance(), &ChatStore::accountChanged, this, [this] {
    Reset();
    if (testAttribute(Qt::WA_DeleteOnClose))
      QDialog::reject();
  });
  const auto accessChanged = [this] {
    ++_revision;
    ui->results_list->clear();
    ui->next_btn->setEnabled(false);
    ui->previous_btn->setEnabled(false);
    QTimer::singleShot(0, this, [this] {
      if (_global)
        RefreshScope();
      if (isVisible())
        Query();
    });
  };
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_friend_deleted, this, accessChanged);
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_group_info, this, accessChanged);
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_contact_tags_updated, this, accessChanged);
  Reset();
}

ChatHistoryDialog::~ChatHistoryDialog() { delete ui; }

void ChatHistoryDialog::SetConversation(int threadId, const QString &name)
{
  if (_global || _threadId != threadId)
    Reset();
  _threadId = threadId;
  setWindowTitle(tr("与“%1”的聊天记录").arg(name));
  Query();
}

QVector<int> ChatHistoryDialog::AllowedThreads() const
{
  QVector<int> ids;
  const auto mgr = UserMgr::GetInstance();
  for (const auto &thread : mgr->GetChatThreads())
    if (thread && (thread->IsGroup() || mgr->GetFriendById(thread->GetOtherId())))
      ids.append(thread->GetThreadId());
  std::sort(ids.begin(), ids.end());
  return ids;
}

QString ChatHistoryDialog::ThreadName(int id) const
{
  const auto mgr = UserMgr::GetInstance();
  const auto thread = mgr->GetChatThreadByThreadId(id);
  if (!thread)
    return tr("不可用会话");
  const auto user = mgr->GetFriendById(thread->GetOtherId());
  return thread->IsGroup() ? thread->GetGroupName()
         : user            ? (user->_back.isEmpty() ? user->_name : user->_back)
                           : tr("不可用会话");
}

void ChatHistoryDialog::RefreshScope()
{
  const QSignalBlocker blocker(_scope);
  const int previous = _threadId;
  _scope->clear();
  _scope->addItem(tr("所有可用会话（仅本机记录）"), 0);
  for (int id : AllowedThreads())
    _scope->addItem(ThreadName(id) + tr(" · 会话 %1").arg(id), id);
  _scope->setCurrentIndex(qMax(0, _scope->findData(previous)));
  _threadId = _scope->currentData().toInt();
}

void ChatHistoryDialog::SetAllConversations()
{
  if (!_global)
    Reset();
  _global = true;
  RefreshScope();
  _scope->show();
  setWindowTitle(tr("所有聊天记录 · 仅本机"));
  Query();
}

void ChatHistoryDialog::Reset()
{
  _exporter->Cancel();
  _exportStatus->clear();
  for (auto *dialog : findChildren<QDialog *>())
    dialog->reject();
  hide();
  ++_revision;
  _global = false;
  _scope->hide();
  _scope->clear();
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
  if (_exporter->Busy())
    return;
  _searchTimer->stop();
  if (!_global && _threadId <= 0)
    return;
  const auto allowed = AllowedThreads();
  if (_threadId > 0 && !allowed.contains(_threadId)) {
    ++_revision;
    ui->results_list->clear();
    ui->results_list->hide();
    ui->records_scroll->show();
    ui->empty_title->setText(tr("会话已不可用"));
    ui->previous_btn->setEnabled(false);
    ui->next_btn->setEnabled(false);
    return;
  }
  if (reset) {
    _pageIndex = 0;
    _pageCursors = {HistoryCursor{}};
  }
  const auto revision = ++_revision;
  const QPointer<ChatHistoryDialog> guard(this);
  ui->next_btn->setEnabled(false);
  ui->previous_btn->setEnabled(false);
  ui->results_list->clear();
  ChatStore::instance()->SearchMessages(
      _threadId, ui->search_edit->text().trimmed(), _type, _pageCursors[_pageIndex],
      [guard, revision, allowed](const ChatStoreResult &result) {
        if (!guard || guard->_revision != revision || guard->_exporter->Busy())
          return;
        if (guard->AllowedThreads() != allowed) {
          if (guard->_global)
            guard->RefreshScope();
          guard->Query();
          return;
        }
        guard->Display(result);
      },
      _date, allowed);
}

void ChatHistoryDialog::done(int result)
{
  _exporter->Cancel();
  QDialog::done(result);
}

void ChatHistoryDialog::SetExportBusy(bool busy)
{
  ui->history_toolbar->setEnabled(!busy);
  _scope->setEnabled(!busy);
  ui->previous_btn->setEnabled(false);
  ui->next_btn->setEnabled(false);
  ui->refresh_btn->setEnabled(!busy);
  _exportButton->setText(busy ? tr("取消导出") : tr("导出记录"));
  if (!busy && isVisible())
    Query();
}

void ChatHistoryDialog::BeginExport()
{
  if (_exporter->Busy()) {
    _exporter->Cancel();
    return;
  }
  if ((!_global && _threadId <= 0) || !ChatStore::instance()->IsReady())
    return;
  Query();
  const HistoryFilter filter{
      _threadId, ui->search_edit->text().trimmed(), _type, _date, AllowedThreads(), true};
  const auto generation = ChatStore::instance()->Generation(), revision = _revision;
  const QPointer<ChatHistoryDialog> guard(this);
  QPointer<QFileDialog> picker = new QFileDialog(this, tr("导出已确认记录（明文，不含附件文件）"));
  picker->setAcceptMode(QFileDialog::AcceptSave);
  picker->setFileMode(QFileDialog::AnyFile);
  picker->setNameFilters({tr("JSON 文件 (*.json)"), tr("UTF-8 文本 (*.txt)")});
  picker->setDefaultSuffix("json");
  picker->setDirectory(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
  picker->selectFile("CppChat-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
  connect(picker, &QFileDialog::filterSelected, picker, [picker](const QString &selected) {
    if (picker)
      picker->setDefaultSuffix(selected.contains("*.json") ? "json" : "txt");
  });
  const bool accepted = picker->exec() == QDialog::Accepted;
  const auto destination = picker ? picker->selectedFiles().value(0) : QString();
  const auto format = picker && picker->selectedNameFilter().contains("*.json")
                          ? HistoryExporter::Format::Json
                          : HistoryExporter::Format::Text;
  if (picker)
    picker->deleteLater();
  if (!accepted || !guard)
    return;
  if (generation != ChatStore::instance()->Generation() || revision != _revision ||
      filter.allowedThreads != AllowedThreads()) {
    _exportStatus->setText(tr("账号、权限或筛选已变化，请重新导出"));
    return;
  }
  QString error;
  if (!_exporter->Start(filter, destination, format, error))
    _exportStatus->setText(error);
}

void ChatHistoryDialog::Display(const ChatStoreResult &result)
{
  ui->results_list->clear();
  ui->results_list->setVisible(!result.messages.isEmpty());
  ui->records_scroll->setVisible(result.messages.isEmpty());
  ui->empty_title->setText(result.error.isEmpty() ? tr("没有找到记录") : tr("读取失败"));
  ui->empty_description->setText(
      result.error.isEmpty() ? tr("当前查询仅包含已同步到本机的记录；后台同步完成后重新查询。")
                             : result.error);
  _nextCursor = result.nextCursor;
  ui->previous_btn->setEnabled(_pageIndex > 0);
  ui->next_btn->setEnabled(result.more);
  for (const auto &message : result.messages)
    AppendMessage(message);
}

void ChatHistoryDialog::AppendMessage(const StoredMessage &message)
{
  auto *row = new QWidget;
  row->installEventFilter(this);
  auto *layout = new QVBoxLayout(row);
  const auto friendInfo = UserMgr::GetInstance()->GetFriendById(message.senderUid);
  const auto thread = UserMgr::GetInstance()->GetChatThreadByThreadId(message.threadId);
  const auto name = message.senderUid == UserMgr::GetInstance()->GetUid() ? tr("我")
                    : friendInfo
                        ? (friendInfo->_back.isEmpty() ? friendInfo->_name : friendInfo->_back)
                    : thread && thread->IsGroup() ? thread->MemberName(message.senderUid)
                                                  : QString::number(message.senderUid);
  auto *label = new QLabel(
      name + "  " + message.time + (message.serverId == 0 ? tr("  待确认") : QString()), row);
  label->setTextFormat(Qt::PlainText);
  layout->addWidget(label);
  if (_global) {
    auto *open = new QPushButton(tr("打开会话：%1").arg(ThreadName(message.threadId)), row);
    open->setObjectName("openHistoryConversationButton");
    open->setProperty("threadId", message.threadId);
    open->setAutoDefault(false);
    layout->insertWidget(0, open);
    connect(open, &QPushButton::clicked, this, [this, id = message.threadId] {
      if (AllowedThreads().contains(id))
        emit openThreadRequested(id);
    });
  }
  if (message.type == 1) {
    auto *picture = new PictureBubble(ChatRole::Other);
    layout->addWidget(picture);
    const auto fileId = message.content;
    picture->loadImage(fileId);
  } else if (message.type == 3) {
    auto *audio = new AudioBubble(ChatRole::Other);
    layout->addWidget(audio);
    audio->Load(message.content);
  } else
    layout->addWidget(new TextBubble(ChatRole::Other, message.content));
  auto *item = new QListWidgetItem(ui->results_list);
  item->setSizeHint(row->sizeHint().expandedTo(QSize(320, message.type == 1 ? 220 : 86)));
  ui->results_list->setItemWidget(item, row);
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
