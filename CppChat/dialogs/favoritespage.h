#ifndef FAVORITESPAGE_H
#define FAVORITESPAGE_H

#include <QWidget>

namespace Ui {
class FavoritesPage;
}

// 收藏页目前只负责展示 UI 占位，搜索、分类和收藏数据接口后续再接入。
class FavoritesPage : public QWidget {
  Q_OBJECT

public:
  explicit FavoritesPage(QWidget *parent = nullptr);
  ~FavoritesPage() override;
  void Reset();

private:
  Ui::FavoritesPage *ui;
};

#endif // FAVORITESPAGE_H
