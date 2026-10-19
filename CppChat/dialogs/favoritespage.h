#ifndef FAVORITESPAGE_H
#define FAVORITESPAGE_H

#include "common/favorite.h"
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <QWidget>
class QListWidget;
class QLabel;
class QPushButton;
class QDialog;

namespace Ui {
class FavoritesPage;
}

// Account-local favorites. SQL and media transfer remain in their existing services.
class FavoritesPage : public QWidget {
  Q_OBJECT

public:
  explicit FavoritesPage(QWidget *parent = nullptr);
  ~FavoritesPage() override;
  void Reset();

signals:
  void resendRequested(QVector<StoredFavorite> snapshots);
  void notesRequested();
  void remindersRequested();

protected:
  void showEvent(QShowEvent *event) override;

private:
  Ui::FavoritesPage *ui;
  void Load(bool append = false);
  void Open(bool resend);
  void Remove();
  void UpdateControls();
  QListWidget *_list;
  QLabel *_status;
  QPushButton *_open, *_send, *_remove, *_more;
  QPointer<QDialog> _preview;
  QTimer _search;
  FavoriteCategory _category = FavoriteCategory::All;
  quint64 _revision = 0;
  qint64 _cursor = 0;
  bool _busy = false, _hasMore = false;
};

#endif // FAVORITESPAGE_H
