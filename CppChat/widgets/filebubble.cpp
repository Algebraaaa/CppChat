#include "filebubble.h"
#include "common/chatstore.h"
#include "network/resourcemgr.h"
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrentRun>

FileBubble::FileBubble(ChatRole role, QWidget *parent) : BubbleFrame(role, parent)
{
  setObjectName("fileBubble");
  auto *body = new QWidget(this);
  body->setMinimumWidth(210);
  body->setMaximumWidth(280);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(10, 8, 10, 8);
  _status = new QLabel(tr("文件（最多 20 MiB）"), body);
  _status->setTextFormat(Qt::PlainText);
  _status->setWordWrap(true);
  _status->setObjectName("fileMessageInfo");
  _open = new QPushButton(tr("保存并打开"), body);
  _open->setObjectName("fileMessageOpen");
  _save = new QPushButton(tr("下载文件"), body);
  _save->setObjectName("fileMessageSave");
  layout->addWidget(_status);
  auto *actions = new QHBoxLayout;
  for (auto *button : {_open, _save}) {
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    actions->addWidget(button);
  }
  layout->addLayout(actions);
  setWidget(body);
  connect(_save, &QPushButton::clicked, this, [this] { Activate(Action::Save); });
  connect(_open, &QPushButton::clicked, this, [this] { Activate(Action::Open); });
  connect(ResourceMgr::instance(), &ResourceMgr::imageReady, this,
          [this](const QString &id, const QByteArray &bytes, const QString &error) {
            if (_loading && id == _id && _generation == ChatStore::instance()->Generation())
              Ready(bytes, error);
          });
  connect(ResourceMgr::instance(), &ResourceMgr::downloadProgress, this,
          [this](const QString &id, qint64 done, qint64 total) {
            if (_loading && id == _id)
              _status->setText(tr("下载 %1/%2 KiB").arg(done / 1024).arg((total + 1023) / 1024));
          });
  connect(ChatStore::instance(), &ChatStore::accountChanged, this, [this] {
    _document = {};
    _savedPath.clear();
    _afterLoad = Action::None;
    _loading = false;
    _save->setEnabled(false);
    _open->setEnabled(false);
  });
}
void FileBubble::Load(const QString &fileId)
{
  _id = fileId;
  _generation = ChatStore::instance()->Generation();
  _document = {};
  _loading = false;
  _afterLoad = Action::None;
  _savedPath.clear();
  _status->setText(tr("文件附件 · 最多 20 MiB"));
  _save->setText(tr("另存为…"));
  _open->setText(tr("保存并打开"));
  _save->setEnabled(!fileId.isEmpty());
  _open->setEnabled(!fileId.isEmpty());
  // Outgoing media is already cached; do not download every historical file.
  const auto bytes = ResourceMgr::instance()->cachedImage(fileId);
  if (!bytes.isEmpty())
    Ready(bytes, {});
}
void FileBubble::Ready(const QByteArray &bytes, const QString &error)
{
  _loading = false;
  _save->setEnabled(true);
  _open->setEnabled(true);
  const auto action = _afterLoad;
  _afterLoad = Action::None;
  _document = error.isEmpty() ? FileAttachment::Unpack(bytes) : FileAttachment::Document{};
  if (!_document.valid) {
    _status->setText(error.isEmpty() ? tr("文件内容无效，请重试") : error);
    _save->setText(tr("重试下载"));
    return;
  }
  const auto size = _document.bytes.size() < 1024
                        ? tr("%1 B").arg(_document.bytes.size())
                        : tr("%1 KiB").arg(_document.bytes.size() / 1024.0, 0, 'f', 1);
  _status->setText(_document.name + '\n' + size);
  _status->setToolTip(_document.name);
  _save->setText(tr("另存为…"));
  if (action != Action::None)
    Activate(action);
}
void FileBubble::Activate(Action action)
{
  if (_loading || _generation != ChatStore::instance()->Generation())
    return;
  if (!_document.valid) {
    _afterLoad = action;
    _loading = true;
    _save->setEnabled(false);
    _open->setEnabled(false);
    _status->setText(tr("正在读取文件…"));
    ResourceMgr::instance()->fetchMedia(_id);
    return;
  }
  if (action == Action::Open && !_savedPath.isEmpty() && QFileInfo(_savedPath).isFile()) {
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(_savedPath)))
      _status->setText(tr("无法打开文件，请检查默认打开程序"));
    return;
  }
  Save(action == Action::Open);
}
void FileBubble::Save(bool openAfterSave)
{
  auto *picker = new QFileDialog(this, openAfterSave ? tr("保存并打开文件") : tr("另存文件"));
  picker->setObjectName("fileMessageSaveDialog");
  picker->setAttribute(Qt::WA_DeleteOnClose);
  picker->setAcceptMode(QFileDialog::AcceptSave);
  picker->setFileMode(QFileDialog::AnyFile);
  picker->selectFile(_document.name);
  const auto generation = _generation;
  const auto document = _document;
  connect(picker, &QFileDialog::fileSelected, this,
          [this, generation, document, openAfterSave](const QString &path) {
            if (generation != ChatStore::instance()->Generation())
              return;
            auto *watcher = new QFutureWatcher<QString>(this);
            connect(watcher, &QFutureWatcher<QString>::finished, this,
                    [this, watcher, generation, path, openAfterSave] {
                      const auto error = watcher->result();
                      watcher->deleteLater();
                      if (generation == ChatStore::instance()->Generation()) {
                        _status->setText(error.isEmpty() ? tr("文件已保存") : error);
                        if (error.isEmpty()) {
                          _savedPath = path;
                          _open->setText(tr("打开文件"));
                          if (openAfterSave &&
                              !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
                            _status->setText(tr("文件已保存，但无法启动默认打开程序"));
                        }
                      }
                    });
            watcher->setFuture(QtConcurrent::run([document, path] {
              QString error;
              FileAttachment::Save(document, path, error);
              return error;
            }));
          });
  picker->open();
}
