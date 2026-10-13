#ifndef EMOJIPICKER_H
#define EMOJIPICKER_H

#include <QFrame>
#include <QHash>
#include <QStringList>
#include <QVector>

class QLineEdit;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class QHideEvent;
class QShowEvent;
class QTimer;
struct EmojiEntry;

class EmojiPicker : public QFrame {
  Q_OBJECT
public:
  explicit EmojiPicker(QWidget *parent = nullptr);
  void PopupAt(QWidget *anchor, int uid);

signals:
  void emojiSelected(const QString &id);
  void visibilityChanged(bool visible);

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  void RebuildContent();
  QWidget *CreateGrid(const QVector<const EmojiEntry *> &entries, bool recent = false);
  void SelectCategory(int index);
  void SavePreferences();
  QLineEdit *_search;
  QToolButton *_searchButton;
  QScrollArea *_scroll;
  QVBoxLayout *_contentLayout;
  QVector<QToolButton *> _tabs;
  QStringList _recent;
  QStringList _favorites;
  int _uid = 0;
  int _category = 0;
  int _columns = 10;
  QTimer *_searchTimer;
  QWidget *_buttonHost;
  QHash<QString, QToolButton *> _buttons;
};

#endif
