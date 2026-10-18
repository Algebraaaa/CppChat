#pragma once
#include "common/favorite.h"
#include <QObject>
#include <QTimer>
#include <QVector>
#include <functional>

// Resolve authorized media once for favorites and forwarding, on the GUI thread.
class MessageSnapshotService : public QObject {
  Q_OBJECT
public:
  using Completion = std::function<void(QVector<StoredFavorite>, QString)>;
  explicit MessageSnapshotService(QObject *parent = nullptr);
  bool Collect(QVector<StoredFavorite> snapshots, Completion completion);
  void Cancel();
  bool Busy() const { return _busy; }

private:
  void Next();
  void Loaded(const QByteArray &bytes, const QString &error);
  void Finish(const QString &error);
  bool Current() const;
  QVector<StoredFavorite> _values;
  Completion _completion;
  QTimer _deadline;
  quint64 _generation = 0, _operation = 0;
  qsizetype _bytes = 0, _index = 0;
  QString _waiting;
  bool _busy = false;
  bool _loading = false;
};
