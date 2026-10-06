#ifndef SEARCHLIST_H
#define SEARCHLIST_H

#include "common/userdata.h"
#include <QListWidget>

// 顶部搜索框的本地搜索结果列表。
// 这里只搜索已经加载到客户端的联系人、会话和聊天记录，不再负责向服务器查找陌生人。
class SearchList : public QListWidget
{
  Q_OBJECT

public:
  enum class Scope
  {
    All,
    Contacts
  };
  explicit SearchList(QWidget *parent = nullptr);

  void Search(const QString &keyword, Scope scope = Scope::All);
  void ClearResults();

signals:
  void sig_open_friend(std::shared_ptr<UserInfo> user);
  void sig_open_thread(int threadId);

private:
  enum class ResultKind
  {
    Friend,
    ChatRecord
  };

  void addSection(const QString &title);
  void addResult(ResultKind kind, const std::shared_ptr<UserInfo> &user, const QString &preview,
                 int threadId = 0);
  void addEmptyTip(Scope scope);
  void slotItemClicked(QListWidgetItem *item);
};

#endif // SEARCHLIST_H
