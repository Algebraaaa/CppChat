#ifndef MESSAGEMENU_H
#define MESSAGEMENU_H

#include <QList>
#include <QWidget>

class QHideEvent;
class QKeyEvent;
class QPushButton;
namespace Ui {
class MessageMenu;
}

// 每次右键时创建，关闭后释放；菜单项与尺寸在 .ui 中定义。
class MessageMenu : public QWidget {
  Q_OBJECT
public:
  explicit MessageMenu(QWidget *parent = nullptr);
  ~MessageMenu() override;
  struct Options {
    bool canCopy = false;
    bool showRetry = false;
    bool canRetry = false;
    bool textMessage = false;
    bool canQuote = false;
    bool canDelete = false;
    bool canCollect = false;
  };
  void Popup(const QPoint &position, const Options &options);

signals:
  void copyRequested();
  void retryRequested();
  void readRequested();
  void quoteRequested();
  void deleteRequested();
  void favoriteRequested();
  void forwardRequested();
  void searchRequested();
  void selectRequested();
  void placeholderRequested(const QString &title);

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  Ui::MessageMenu *ui;
  QList<QPushButton *> _buttons;
};

#endif
