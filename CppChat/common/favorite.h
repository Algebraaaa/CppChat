#pragma once
#include <QByteArray>
#include <QMetaType>
#include <QString>

enum class FavoriteCategory { All, Recent, Links, Images, Messages, Audio };

// An account-local snapshot, independent of chat history and its attachment cache.
struct StoredFavorite {
  qint64 id = 0;
  qint64 createdMs = 0;
  int threadId = 0;
  int serverId = 0;
  int senderUid = 0;
  int type = 0;
  QString senderName;
  QString content;
  QByteArray media;
};
Q_DECLARE_METATYPE(StoredFavorite)
