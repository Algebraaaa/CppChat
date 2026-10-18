#include "messagesnapshotservice.h"
#include "common/chatstore.h"
#include "media/pcmwave.h"
#include "network/resourcemgr.h"
#include <QPointer>
#include <utility>

MessageSnapshotService::MessageSnapshotService(QObject *parent) : QObject(parent)
{
  _deadline.setSingleShot(true);
  _deadline.setInterval(300000);
  connect(ChatStore::instance(), &ChatStore::accountChanged, this, &MessageSnapshotService::Cancel);
  connect(&_deadline, &QTimer::timeout, this, [this] { Finish(tr("读取媒体超时，请稍后重试")); });
  connect(ResourceMgr::instance(), &ResourceMgr::imageReady, this,
          [this](const QString &id, const QByteArray &bytes, const QString &error) {
            if (_busy && Current() && !_waiting.isEmpty() && _waiting == id)
              Loaded(bytes, error);
          });
}

bool MessageSnapshotService::Current() const
{
  return ChatStore::instance()->IsReady() && ChatStore::instance()->Generation() == _generation;
}

bool MessageSnapshotService::Collect(QVector<StoredFavorite> snapshots, Completion completion)
{
  if (_busy && !Current())
    Cancel();
  if (_busy || !ChatStore::instance()->IsReady() || snapshots.isEmpty() || snapshots.size() > 50)
    return false;
  _busy = true;
  _values = std::move(snapshots);
  _completion = std::move(completion);
  _generation = ChatStore::instance()->Generation();
  _bytes = _index = 0;
  ++_operation;
  QTimer::singleShot(0, this, &MessageSnapshotService::Next);
  return true;
}

void MessageSnapshotService::Cancel()
{
  ++_operation;
  _deadline.stop();
  _values.clear();
  _completion = {};
  _waiting.clear();
  _busy = false;
  _loading = false;
}

void MessageSnapshotService::Next()
{
  if (!_busy || _loading)
    return;
  if (!Current()) {
    Cancel();
    return;
  }
  while (_index < _values.size()) {
    auto &value = _values[_index];
    if (value.type == 0) {
      ++_index;
      continue;
    }
    if (value.type != 1 && value.type != 3) {
      Finish(tr("当前版本尚不支持此媒体类型"));
      return;
    }
    if (!value.media.isEmpty()) {
      Loaded(value.media, {});
      return;
    }
    const auto cached = ResourceMgr::instance()->cachedImage(value.content);
    if (!cached.isEmpty()) {
      Loaded(cached, {});
      return;
    }
    const QPointer<MessageSnapshotService> guard(this);
    const auto operation = _operation;
    const QString file = value.content;
    _loading = true;
    ChatStore::instance()->LoadAttachment(file, [guard, operation, file](const ChatStoreResult &r) {
      if (!guard || !guard->_busy || guard->_operation != operation || !guard->Current())
        return;
      if (r.error.isEmpty() && !r.image.isEmpty()) {
        guard->Loaded(r.image, {});
      } else {
        guard->_waiting = file;
        guard->_deadline.start();
        ResourceMgr::instance()->fetchImage(file);
      }
    });
    return;
  }
  Finish({});
}

void MessageSnapshotService::Loaded(const QByteArray &bytes, const QString &error)
{
  if (!_busy || !Current() || _index >= _values.size())
    return;
  _loading = false;
  _deadline.stop();
  _waiting.clear();
  if (!error.isEmpty() || bytes.isEmpty() || bytes.size() > 20 * 1024 * 1024 ||
      _bytes + bytes.size() > 64 * 1024 * 1024 ||
      (_values[_index].type == 3 &&
       cppchat::media::WaveDurationMs(std::string_view(bytes.constData(), bytes.size()),
                                      bytes.size()) == 0)) {
    Finish(tr("无法读取媒体或批量媒体超过限制，内容没有提交"));
    return;
  }
  _bytes += bytes.size();
  _values[_index++].media = bytes;
  QTimer::singleShot(0, this, &MessageSnapshotService::Next);
}

void MessageSnapshotService::Finish(const QString &error)
{
  if (!_busy)
    return;
  auto completion = std::move(_completion);
  auto values = error.isEmpty() ? std::move(_values) : QVector<StoredFavorite>{};
  Cancel();
  if (completion)
    completion(std::move(values), error);
}
