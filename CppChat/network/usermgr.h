#ifndef USERMGR_H
#define USERMGR_H
#include "common/singleton.h"
#include "common/userdata.h"
#include <QObject>
#include <memory>
#include <vector>
class UserMgr : public QObject,
                public Singleton<UserMgr>,
                public std::enable_shared_from_this<UserMgr> {
  Q_OBJECT
public:
  friend class Singleton<UserMgr>;
  ~UserMgr();
  void SetName(QString name);
  void SetUid(int uid);
  void SetToken(QString token);
  const QString GetName() const;
  std::vector<std::shared_ptr<ApplyInfo>> GetApplyList();

private:
  UserMgr();
  QString _name;
  QString _token;
  int _uid;
  std::vector<std::shared_ptr<ApplyInfo>> _apply_list;
};

#endif // USERMGR_H
