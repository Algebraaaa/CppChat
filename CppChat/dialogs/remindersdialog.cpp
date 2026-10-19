#include "remindersdialog.h"
#include "common/chatstore.h"
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

RemindersDialog::RemindersDialog(QWidget *parent) : QDialog(parent)
{
  setObjectName("remindersDialog");
  setProperty("groupDialog", true);
  setWindowTitle(tr("本机消息提醒"));
  resize(QSize(740, 560).boundedTo(screen()->availableGeometry().size() - QSize(40, 60)));
  auto *layout = new QVBoxLayout(this);
  auto *hint =
      new QLabel(tr("客户端登录运行时检查到期提醒；退出后停止，重登显示未处理的到期项。"), this);
  hint->setWordWrap(true);
  layout->addWidget(hint);
  _search = new QLineEdit(this);
  _search->setObjectName("remindersSearch");
  _search->setPlaceholderText(tr("搜索提醒摘要"));
  _search->setMaxLength(256);
  layout->addWidget(_search);
  _list = new QListWidget(this);
  _list->setObjectName("remindersList");
  _list->setWordWrap(true);
  layout->addWidget(_list, 1);
  _status = new QLabel(this);
  _status->setObjectName("remindersStatus");
  _status->setTextFormat(Qt::PlainText);
  _status->setWordWrap(true);
  layout->addWidget(_status);
  auto *buttons = new QDialogButtonBox(this);
  _change = buttons->addButton(tr("修改时间"), QDialogButtonBox::ActionRole);
  _change->setObjectName("reminderChange");
  _remove = buttons->addButton(tr("完成 / 取消"), QDialogButtonBox::ActionRole);
  _remove->setObjectName("reminderRemove");
  _open = buttons->addButton(tr("打开会话"), QDialogButtonBox::ActionRole);
  _open->setObjectName("reminderOpen");
  auto *refresh = buttons->addButton(tr("刷新"), QDialogButtonBox::ActionRole);
  refresh->setObjectName("reminderRefresh");
  buttons->addButton(tr("关闭"), QDialogButtonBox::RejectRole);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(refresh, &QPushButton::clicked, this, &RemindersDialog::Reload);
  connect(_search, &QLineEdit::textChanged, this, &RemindersDialog::Filter);
  connect(_list, &QListWidget::itemSelectionChanged, this, &RemindersDialog::Controls);
  connect(_change, &QPushButton::clicked, this, [this] {
    if (_list->currentItem())
      Schedule(_values.value(_list->currentItem()->data(Qt::UserRole).toLongLong()));
  });
  connect(_open, &QPushButton::clicked, this, [this] {
    if (_list->currentItem())
      emit openThreadRequested(
          _values.value(_list->currentItem()->data(Qt::UserRole).toLongLong()).threadId);
  });
  connect(_remove, &QPushButton::clicked, this, &RemindersDialog::Remove);
  connect(ChatStore::instance(), &ChatStore::remindersChanged, this, &RemindersDialog::Reload);
  connect(ChatStore::instance(), &ChatStore::accountChanged, this, [this] {
    ++_revision;
    _values.clear();
    _list->clear();
    if (_editor)
      _editor->reject();
    reject();
  });
  Reload();
}
void RemindersDialog::Controls()
{
  const bool selected = !_busy && _list->currentItem() && !_list->currentItem()->isHidden();
  for (auto *button : {_change, _remove, _open})
    button->setEnabled(selected);
}
void RemindersDialog::Filter()
{
  const auto query = _search->text().trimmed();
  for (int i = 0; i < _list->count(); ++i)
    _list->item(i)->setHidden(!_list->item(i)->text().contains(query, Qt::CaseInsensitive));
  Controls();
}
void RemindersDialog::Reload()
{
  const auto revision = ++_revision;
  _values.clear();
  _list->clear();
  if (!ChatStore::instance()->IsReady()) {
    _status->setText(tr("请登录并等待本机记录加载完成"));
    Controls();
    return;
  }
  _busy = true;
  Controls();
  const QPointer<RemindersDialog> guard(this);
  ChatStore::instance()->LoadReminders([guard, revision](const ChatStoreResult &result) {
    if (!guard || guard->_revision != revision)
      return;
    guard->_busy = false;
    int due = 0;
    for (const auto &value : result.reminders) {
      guard->_values.insert(value.id, value);
      due += value.due;
      auto *item = new QListWidgetItem(
          (value.due ? guard->tr("[到期] ") : QString()) + value.subject + '\n' +
              QDateTime::fromMSecsSinceEpoch(value.dueMs).toString("yyyy-MM-dd HH:mm:ss") + '\n' +
              value.excerpt,
          guard->_list);
      item->setData(Qt::UserRole, value.id);
      item->setSizeHint(QSize(0, 116));
    }
    guard->_status->setText(
        result.error.isEmpty()
            ? guard->tr("共 %1 条提醒，%2 条到期未处理").arg(result.reminders.size()).arg(due)
            : guard->tr("提醒读取失败，请刷新重试"));
    guard->Filter();
  });
}
void RemindersDialog::Schedule(const LocalReminder &value)
{
  if (!ChatStore::instance()->IsReady())
    return;
  if (_editor) {
    _editor->show();
    _editor->raise();
    return;
  }
  auto *dialog = new QDialog(this);
  dialog->setObjectName("reminderScheduleDialog");
  dialog->setProperty("groupDialog", true);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(value.id ? tr("修改提醒时间") : tr("设置消息提醒"));
  dialog->resize(540, 360);
  auto *layout = new QVBoxLayout(dialog);
  auto *preview = new QPlainTextEdit(dialog);
  preview->setReadOnly(true);
  preview->setObjectName("reminderPreview");
  preview->setPlainText(value.subject + '\n' + value.excerpt);
  layout->addWidget(preview, 1);
  auto *time = new QDateTimeEdit(dialog);
  time->setObjectName("reminderTime");
  time->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
  time->setCalendarPopup(true);
  const auto now = QDateTime::currentDateTime();
  time->setMinimumDateTime(now.addSecs(1));
  time->setMaximumDateTime(now.addDays(365));
  time->setDateTime(value.dueMs > now.toMSecsSinceEpoch()
                        ? QDateTime::fromMSecsSinceEpoch(value.dueMs)
                        : now.addSecs(300));
  layout->addWidget(time);
  auto *status = new QLabel(dialog);
  status->setObjectName("reminderScheduleStatus");
  status->setWordWrap(true);
  layout->addWidget(status);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog);
  auto *save = buttons->button(QDialogButtonBox::Save);
  save->setObjectName("reminderSave");
  save->setText(tr("保存提醒"));
  buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(save, &QPushButton::clicked, dialog, [dialog, time, status, save, value] {
    auto reminder = value;
    reminder.dueMs = time->dateTime().toMSecsSinceEpoch();
    time->setEnabled(false);
    save->setEnabled(false);
    const QPointer<QDialog> guard(dialog);
    ChatStore::instance()->SaveReminder(
        reminder, [guard, time, status, save](const ChatStoreResult &result) {
          if (!guard)
            return;
          if (result.error.isEmpty())
            guard->accept();
          else {
            status->setText(
                QObject::tr("保存失败：请选择未来时间，确认原消息仍存在或提醒数量未达上限"));
            time->setEnabled(true);
            save->setEnabled(true);
          }
        });
  });
  _editor = dialog;
  dialog->show();
}
void RemindersDialog::Remove()
{
  if (_busy || !_list->currentItem())
    return;
  const auto id = _list->currentItem()->data(Qt::UserRole).toLongLong();
  const auto generation = ChatStore::instance()->Generation();
  const QPointer<RemindersDialog> self(this);
  if (QMessageBox::question(this, tr("移除提醒"), tr("完成或取消这条本机提醒？原消息不会删除。"),
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No) != QMessageBox::Yes ||
      !self || ChatStore::instance()->Generation() != generation)
    return;
  _busy = true;
  Controls();
  const QPointer<RemindersDialog> guard(this);
  ChatStore::instance()->RemoveReminder(id, [guard](const ChatStoreResult &result) {
    if (!guard)
      return;
    guard->_busy = false;
    if (!result.error.isEmpty()) {
      guard->_status->setText(guard->tr("移除失败，提醒仍保留"));
      guard->Controls();
    }
  });
}
