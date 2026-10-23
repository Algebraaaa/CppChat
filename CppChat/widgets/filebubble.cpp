#include "filebubble.h"
#include "common/chatstore.h"
#include "network/resourcemgr.h"
#include <QFileDialog>
#include <QFutureWatcher>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>

FileBubble::FileBubble(ChatRole role, QWidget *parent) : BubbleFrame(role, parent)
{
  setObjectName("fileBubble");
  auto *body = new QWidget(this);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(10, 8, 10, 8);
  _status = new QLabel(tr("文件（最多 20 MiB）"), body);
  _status->setTextFormat(Qt::PlainText);
  _status->setWordWrap(true);
  _save = new QPushButton(tr("下载文件"), body);
  _save->setObjectName("fileMessageSave");
  layout->addWidget(_status);
  layout->addWidget(_save);
  setWidget(body);
  connect(_save, &QPushButton::clicked, this, &FileBubble::FetchOrSave);
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
    _loading = false;
    _save->setEnabled(false);
  });
}
void FileBubble::Load(const QString &fileId)
{
  _id = fileId;
  _generation = ChatStore::instance()->Generation();
  _document = {};
  _loading = false;
}
void FileBubble::Ready(const QByteArray &bytes, const QString &error)
{
  _loading = false;
  _save->setEnabled(true);
  _document = error.isEmpty() ? FileAttachment::Unpack(bytes) : FileAttachment::Document{};
  if (!_document.valid) {
    _status->setText(error.isEmpty() ? tr("文件内容无效，请重试") : error);
    _save->setText(tr("重试下载"));
    return;
  }
  _status->setText(tr("%1 · %2 KiB").arg(_document.name).arg(_document.bytes.size() / 1024.0, 0, 'f', 1));
  _save->setText(tr("另存为…"));
}
void FileBubble::FetchOrSave()
{
  if (_loading || _generation != ChatStore::instance()->Generation())
    return;
  if (!_document.valid) {
    _loading = true;
    _save->setEnabled(false);
    _status->setText(tr("正在读取文件…"));
    ResourceMgr::instance()->fetchMedia(_id);
    return;
  }
  auto *picker = new QFileDialog(this, tr("保存收到的文件"));
  picker->setAttribute(Qt::WA_DeleteOnClose);
  picker->setAcceptMode(QFileDialog::AcceptSave);
  picker->setFileMode(QFileDialog::AnyFile);
  picker->selectFile(_document.name);
  const auto generation = _generation;
  const auto document = _document;
  connect(picker, &QFileDialog::fileSelected, this, [this, generation, document](const QString &path) {
    if (generation != ChatStore::instance()->Generation())
      return;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, generation] {
      const auto error = watcher->result();
      watcher->deleteLater();
      if (generation == ChatStore::instance()->Generation())
        _status->setText(error.isEmpty() ? tr("文件已保存") : error);
    });
    watcher->setFuture(QtConcurrent::run([document, path] {
      QString error;
      FileAttachment::Save(document, path, error);
      return error;
    }));
  });
  picker->open();
}
