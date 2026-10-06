#ifndef DISCOVERPAGE_H
#define DISCOVERPAGE_H

#include <QWidget>

namespace Ui {
class DiscoverPage;
}

// 发现页只展示入口和占位提示，后续业务功能从按钮点击处接入。
class DiscoverPage : public QWidget
{
  Q_OBJECT
public:
  explicit DiscoverPage(QWidget *parent = nullptr);
  ~DiscoverPage() override;
  void Reset();

private:
  Ui::DiscoverPage *ui;
};

#endif // DISCOVERPAGE_H
