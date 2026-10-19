#include "notesdialog.h"
#include "common/chatstore.h"
#include <QCloseEvent>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

NotesDialog::NotesDialog(QWidget *parent) : QDialog(parent)
{
  setObjectName("notesDialog");
  setProperty("groupDialog", true);
  setWindowTitle(tr("本机笔记"));
  resize(QSize(900, 620).boundedTo(screen()->availableGeometry().size() - QSize(40, 60)));
  auto *layout = new QVBoxLayout(this);
  auto *splitter = new QSplitter(this);
  auto *left = new QWidget(splitter);
  auto *listLayout = new QVBoxLayout(left);
  _search = new QLineEdit(left);
  _search->setObjectName("notesSearch");
  _search->setPlaceholderText(tr("搜索标题或正文"));
  _search->setMaxLength(256);
  _list = new QListWidget(left);
  _list->setObjectName("notesList");
  _list->setWordWrap(true);
  _list->setUniformItemSizes(true);
  listLayout->addWidget(_search);
  listLayout->addWidget(_list, 1);
  auto *listButtons = new QHBoxLayout;
  const auto button = [](const QString &name, const QString &text, QLayout *row, QWidget *owner) {
    auto *value = new QPushButton(text, owner);
    value->setObjectName(name);
    if (row)
      row->addWidget(value);
    return value;
  };
  _new = button("noteNew", tr("新建"), listButtons, left);
  _open = button("noteOpen", tr("打开所选"), listButtons, left);
  _more = button("notesMore", tr("更多"), listButtons, left);
  listLayout->addLayout(listButtons);
  auto *right = new QWidget(splitter);
  auto *editor = new QVBoxLayout(right);
  _title = new QLineEdit(right);
  _title->setObjectName("noteTitle");
  _title->setMaxLength(80);
  _title->setPlaceholderText(tr("笔记标题，最多 80 字符"));
  _text = new QPlainTextEdit(right);
  _text->setObjectName("noteBody");
  _text->setPlaceholderText(tr("纯文本笔记，仅保存在本机当前账号"));
  editor->addWidget(_title);
  editor->addWidget(_text, 1);
  _length = new QLabel(right);
  _length->setTextFormat(Qt::PlainText);
  editor->addWidget(_length);
  auto *editButtons = new QGridLayout;
  _save = button("noteSave", tr("保存"), nullptr, right);
  _saveAs = button("noteSaveAs", tr("另存为新笔记"), nullptr, right);
  _send = button("noteSend", tr("发送这篇笔记给…"), nullptr, right);
  _remove = button("noteDelete", tr("删除这篇笔记"), nullptr, right);
  editButtons->addWidget(_save, 0, 0);
  editButtons->addWidget(_saveAs, 0, 1);
  editButtons->addWidget(_send, 1, 0);
  editButtons->addWidget(_remove, 1, 1);
  editor->addLayout(editButtons);
  splitter->setStretchFactor(0, 1);
  splitter->setStretchFactor(1, 2);
  layout->addWidget(splitter, 1);
  _status = new QLabel(this);
  _status->setObjectName("notesStatus");
  _status->setTextFormat(Qt::PlainText);
  _status->setWordWrap(true);
  layout->addWidget(_status);
  auto *close = new QDialogButtonBox(QDialogButtonBox::Close, this);
  close->button(QDialogButtonBox::Close)->setText(tr("关闭"));
  layout->addWidget(close);
  connect(close, &QDialogButtonBox::rejected, this, &NotesDialog::reject);
  _searchTimer.setSingleShot(true);
  _searchTimer.setInterval(200);
  connect(_search, &QLineEdit::textChanged, this, [this] {
    ++_queryRevision;
    _list->clear();
    _cursor = 0;
    _moreAvailable = false;
    Controls();
    _searchTimer.start();
  });
  connect(&_searchTimer, &QTimer::timeout, this, [this] { Load(); });
  connect(_list, &QListWidget::itemActivated, this, [this] { Open(); });
  connect(_list, &QListWidget::itemSelectionChanged, this, &NotesDialog::Controls);
  connect(_new, &QPushButton::clicked, this, &NotesDialog::NewNote);
  connect(_open, &QPushButton::clicked, this, &NotesDialog::Open);
  connect(_more, &QPushButton::clicked, this, [this] { Load(true); });
  connect(_save, &QPushButton::clicked, this, &NotesDialog::Save);
  connect(_saveAs, &QPushButton::clicked, this, [this] {
    if (!_saving) {
      _note.id = 0;
      _note.revision = 0;
      _dirty = true;
      Save();
    }
  });
  connect(_remove, &QPushButton::clicked, this, &NotesDialog::Remove);
  connect(_send, &QPushButton::clicked, this, [this] {
    if (!_dirty && _note.id > 0 && !_saving)
      emit sendRequested(_note.title + '\n' + _note.text);
  });
  const auto edited = [this] {
    _dirty = true;
    Controls();
  };
  connect(_title, &QLineEdit::textChanged, this, edited);
  connect(_text, &QPlainTextEdit::textChanged, this, edited);
  connect(ChatStore::instance(), &ChatStore::accountChanged, this, [this] {
    _discarding = true;
    _list->clear();
    SetNote({});
    QDialog::reject();
  });
  connect(ChatStore::instance(), &ChatStore::notesChanged, this, [this] {
    if (!_saving)
      Load();
  });
  SetNote({});
  Load();
}

void NotesDialog::Controls()
{
  const bool ready = ChatStore::instance()->IsReady() && !_saving;
  const auto bytes = _text->toPlainText().toUtf8().size();
  _length->setText(
      tr("正文 %1 / 32768 字节%2").arg(bytes).arg(_dirty ? tr(" · 尚未保存") : QString()));
  _title->setEnabled(ready);
  _text->setEnabled(ready);
  _new->setEnabled(ready);
  _list->setEnabled(!_saving);
  _save->setEnabled(ready && _dirty && !_title->text().trimmed().isEmpty() && bytes <= 32768);
  _saveAs->setEnabled(ready && !_title->text().trimmed().isEmpty() && bytes <= 32768);
  _send->setEnabled(ready && !_dirty && _note.id > 0);
  _remove->setEnabled(ready && _note.id > 0);
  _open->setEnabled(ready && !_loading && _list->currentItem());
  _more->setVisible(_moreAvailable);
  _more->setEnabled(!_loading && !_saving);
}

bool NotesDialog::CanDiscard()
{
  if (_discarding)
    return true;
  if (_saving) {
    _status->setText(tr("正在保存，请稍候"));
    return false;
  }
  if (!_dirty)
    return true;
  const QPointer<NotesDialog> guard(this);
  const auto generation = ChatStore::instance()->Generation();
  const auto choice = QMessageBox::question(this, tr("尚未保存"), tr("放弃这篇笔记的未保存修改？"),
                                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  return guard && ChatStore::instance()->Generation() == generation && choice == QMessageBox::Yes;
}
void NotesDialog::reject()
{
  if (CanDiscard())
    QDialog::reject();
}
void NotesDialog::closeEvent(QCloseEvent *event)
{
  if (!CanDiscard()) {
    event->ignore();
    return;
  }
  _discarding = true;
  QDialog::closeEvent(event);
  _discarding = false;
}
void NotesDialog::SetNote(const LocalNote &note)
{
  ++_editRevision;
  _note = note;
  const QSignalBlocker title(_title), text(_text);
  _title->setText(note.title);
  _text->setPlainText(note.text);
  _dirty = false;
  Controls();
}
void NotesDialog::NewNote()
{
  if (CanDiscard()) {
    SetNote({});
    _title->setFocus();
  }
}

void NotesDialog::Load(bool append)
{
  if (!ChatStore::instance()->IsReady()) {
    _status->setText(tr("请登录并等待本机记录加载完成"));
    Controls();
    return;
  }
  if (append && (_loading || !_moreAvailable))
    return;
  _searchTimer.stop();
  const auto revision = ++_queryRevision;
  _loading = true;
  if (!append) {
    _list->clear();
    _cursor = 0;
  }
  Controls();
  const QPointer<NotesDialog> guard(this);
  ChatStore::instance()->LoadNotes(
      _search->text(), _cursor, [guard, revision](const ChatStoreResult &result) {
        if (!guard || guard->_queryRevision != revision)
          return;
        guard->_loading = false;
        guard->_moreAvailable = result.error.isEmpty() && result.more;
        for (const auto &note : result.notes) {
          auto *item = new QListWidgetItem(
              note.title + '\n' +
                  QDateTime::fromMSecsSinceEpoch(note.updatedMs).toString("yyyy-MM-dd HH:mm"),
              guard->_list);
          item->setData(Qt::UserRole, note.id);
          item->setSizeHint(QSize(0, 64));
          guard->_cursor = note.id;
        }
        guard->_status->setText(result.error.isEmpty()
                                    ? guard->tr("已加载 %1 篇本机笔记").arg(guard->_list->count())
                                    : guard->tr("读取失败，请重新搜索或打开重试"));
        guard->Controls();
      });
}
void NotesDialog::Open()
{
  if (!_list->currentItem() || _loading)
    return;
  const auto id = _list->currentItem()->data(Qt::UserRole).toLongLong();
  if (!CanDiscard())
    return;
  const auto revision = ++_editRevision;
  _saving = true;
  Controls();
  const QPointer<NotesDialog> guard(this);
  ChatStore::instance()->GetNote(id, [guard, revision](const ChatStoreResult &result) {
    if (!guard || guard->_editRevision != revision)
      return;
    guard->_saving = false;
    if (result.error.isEmpty() && !result.notes.isEmpty())
      guard->SetNote(result.notes.first());
    else {
      guard->_status->setText(guard->tr("笔记已删除或读取失败，当前内容仍保留"));
      guard->Controls();
    }
  });
}
void NotesDialog::Save()
{
  if (_saving || !_dirty)
    return;
  auto value = _note;
  value.title = _title->text();
  value.text = _text->toPlainText();
  _saving = true;
  Controls();
  const QPointer<NotesDialog> guard(this);
  ChatStore::instance()->SaveNote(value, [guard](const ChatStoreResult &result) {
    if (!guard)
      return;
    guard->_saving = false;
    if (result.error.isEmpty() && !result.notes.isEmpty()) {
      guard->SetNote(result.notes.first());
      guard->Load();
      guard->_status->setText(guard->tr("笔记已保存"));
    } else {
      guard->_status->setText(
          guard->tr("保存失败或笔记已在别处修改；当前文字保留，请重新打开或另存为新笔记"));
      guard->Controls();
    }
  });
}
void NotesDialog::Remove()
{
  if (_saving || _note.id <= 0)
    return;
  const auto generation = ChatStore::instance()->Generation();
  const QPointer<NotesDialog> self(this);
  if (QMessageBox::question(
          this, tr("删除笔记"), tr("删除当前编辑的本机笔记？此操作不会删除聊天消息。"),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes ||
      !self || ChatStore::instance()->Generation() != generation)
    return;
  _saving = true;
  Controls();
  const QPointer<NotesDialog> guard(this);
  ChatStore::instance()->DeleteNote(
      _note.id, _note.revision, [guard](const ChatStoreResult &result) {
        if (!guard)
          return;
        guard->_saving = false;
        if (result.error.isEmpty()) {
          guard->SetNote({});
          guard->Load();
        } else {
          guard->_status->setText(guard->tr("删除失败或笔记已变更，当前内容仍保留"));
          guard->Controls();
        }
      });
}
