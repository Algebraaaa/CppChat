#ifndef LISTITEMBASE_H
#define LISTITEMBASE_H
#include "global.h"
#include <QWidget>

class ListItemBase : public QWidget {
  Q_OBJECT
public:
  explicit ListItemBase(QWidget *parent = nullptr);
  void SetItemType(ListItemType itemType);

  ListItemType GetItemType();

private:
  ListItemType _itemType;

protected:
  void paintEvent(QPaintEvent *) override;

public slots:

signals:
};

#endif // LISTITEMBASE_H
