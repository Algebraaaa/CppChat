#ifndef CONVERSATIONPREFERENCES_H
#define CONVERSATIONPREFERENCES_H

#include <QHash>
#include <QSet>

// 本机会话偏好按账号隔离，不改变服务端消息送达状态。
namespace ConversationPreferences {
struct State {
  QSet<int> pinned;
  QSet<int> unread;
  QSet<int> muted;
  QHash<int, int> hidden;
};
State Load(int uid);
void SavePinned(int uid, const QSet<int> &threads);
void SaveUnread(int uid, const QSet<int> &threads);
// 自动未读变化合并写盘；退出账号或手动修改时可以立即落盘。
void Flush();
void RemoveThread(int uid, int threadId);
bool SaveMuted(int uid, const QSet<int> &threads);
bool SaveHidden(int uid, const QHash<int, int> &threads);
} // namespace ConversationPreferences
#endif
