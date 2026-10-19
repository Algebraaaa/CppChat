#pragma once
#include "common/chatstore.h"
#include <QMap>
#include <memory>

class QSaveFile;

// GUI-thread coordinator. SQL pages stay on ChatStore's worker; bounded file chunks use
// the same QSaveFile pattern as ClientUpdateService. No attachment downloads or credentials.
class HistoryExporter : public QObject {
  Q_OBJECT
public:
  enum class Format { Json, Text };
  Q_ENUM(Format)
  explicit HistoryExporter(QObject *parent = nullptr, int messageLimit = 10000,
                           qint64 byteLimit = 32 * 1024 * 1024);
  ~HistoryExporter() override;
  bool Start(HistoryFilter filter, const QString &fileName, Format format, QString &error);
  bool Busy() const;
  void Cancel();
signals:
  void busyChanged(bool busy);
  void progress(int records);
  void finished(const QString &fileName, int records);
  void failed(const QString &error);
  void cancelled();

private:
  bool ScopeValid() const;
  bool Write(const QByteArray &bytes);
  void NextPage();
  void Fail(const QString &message);
  std::unique_ptr<QSaveFile> _file;
  HistoryFilter _filter;
  Format _format = Format::Json;
  HistoryCursor _cursor;
  quint64 _revision = 0, _generation = 0;
  QMap<int, std::shared_ptr<ChatThreadData>> _threads;
  QMap<int, QString> _names;
  int _count = 0, _messageLimit;
  qint64 _bytes = 0, _byteLimit;
};
