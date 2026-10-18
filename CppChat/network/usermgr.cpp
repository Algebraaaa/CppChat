#include "usermgr.h"
#include "common/avatarimage.h"
#include "common/contactpreferences.h"
#include "network/tcpmgr.h"
#include <QJsonDocument>
#include <QLabel>
#include <QTimer>
#include <algorithm>

UserMgr::UserMgr()
{
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_avatar_received, this,
          [this](int uid, const QByteArray &image, bool success) {
            _avatarRequested.remove(uid);
            if (success)
              SetAvatar(uid, image);
          });
}

void UserMgr::Reset()
{
  ++_avatarCacheGeneration;
  _avatarRevisions.clear();
  _user_info.reset();
  _starredFriends.clear();
  _token.clear();
  _apply_list.clear();
  _outgoing_apply_map.clear();
  _friend_list.clear();
  _friend_map.clear();
  _chat_map.clear();
  _uid_to_thread_id.clear();
  _avatars.clear();
  _avatarLoaded.clear();
  _avatarRequested.clear();
  _avatarRequestGeneration.clear();
}

void UserMgr::SetUserInfo(std::shared_ptr<UserInfo> user)
{
  _user_info = user;
  _starredFriends = ContactPreferences::LoadStarred(GetUid(), gate_url_prefix);
}

bool UserMgr::SetStarredFriend(int uid, bool starred)
{
  if (!CheckFriendById(uid) || uid == GetUid() || GetUid() <= 0)
    return false;
  if (_starredFriends.contains(uid) == starred)
    return true;
  auto updated = _starredFriends;
  if (starred)
    updated.insert(uid);
  else
    updated.remove(uid);
  if (!ContactPreferences::SaveStarred(GetUid(), gate_url_prefix, updated))
    return false;
  _starredFriends = updated;
  emit sig_starred_changed(uid, starred);
  return true;
}

int UserMgr::GetUid() const { return _user_info ? _user_info->_uid : 0; }
QString UserMgr::GetName() const { return _user_info ? _user_info->_name : QString(); }
QString UserMgr::GetNick() const { return _user_info ? _user_info->_nick : QString(); }
QString UserMgr::GetIcon() const { return _user_info ? _user_info->_icon : QString(); }
QString UserMgr::GetDesc() const { return _user_info ? _user_info->_desc : QString(); }

void UserMgr::RequestAvatar(int uid)
{
  if (uid <= 0 || _avatarLoaded.contains(uid) || _avatarRequested.contains(uid) ||
      !TcpMgr::GetInstance()->IsConnected())
    return;
  _avatarRequested.insert(uid);
  const int generation = ++_avatarGenerationCounter;
  _avatarRequestGeneration[uid] = generation;
  const QJsonObject request{{"uid", uid}};
  emit TcpMgr::GetInstance()
      -> sig_send_data(ID_GET_AVATAR_REQ, QJsonDocument(request).toJson(QJsonDocument::Compact));
  QTimer::singleShot(15000, this, [this, uid, generation] {
    if (_avatarRequestGeneration.value(uid) == generation)
      _avatarRequested.remove(uid);
  });
}

QPixmap UserMgr::AvatarPixmap(int uid, const QString &fallback)
{
  RequestAvatar(uid);
  if (_avatars.contains(uid))
    return _avatars.value(uid);
  // 默认原图较大，保持一份共享实例，避免每个联系人反复解码同一资源。
  if (!fallback.isEmpty() && fallback != ":/res/head_1.jpg") {
    QPixmap image(fallback);
    if (!image.isNull())
      return image;
  }
  if (_defaultAvatar.isNull())
    _defaultAvatar.load(":/res/head_1.jpg");
  return _defaultAvatar;
}

QPixmap UserMgr::AvatarThumbnail(int uid, const QString &fallback, const QSize &size,
                                 qreal devicePixelRatio)
{
  if (size.isEmpty())
    return {};
  RequestAvatar(uid);
  const qreal ratio = devicePixelRatio > 0 ? devicePixelRatio : 1.0;
  const QString key = QString("cppchat/avatar-thumbnail/%1/%2/%3/%4x%5@%6/%7")
                          .arg(_avatarCacheGeneration)
                          .arg(uid)
                          .arg(_avatarRevisions.value(uid))
                          .arg(size.width())
                          .arg(size.height())
                          .arg(ratio, 0, 'g', 12)
                          .arg(fallback);
  QPixmap thumbnail;
  if (QPixmapCache::find(key, &thumbnail))
    return thumbnail;
  thumbnail = roundedAvatarPixmap(AvatarPixmap(uid, fallback), size, ratio);
  if (!thumbnail.isNull())
    QPixmapCache::insert(key, thumbnail);
  return thumbnail;
}

void UserMgr::RefreshAvatar(int uid)
{
  if (uid <= 0)
    return;
  _avatarLoaded.remove(uid);
  RequestAvatar(uid);
}

void UserMgr::AttachAvatarLabel(QLabel *label, int uid, const QString &fallback)
{
  if (!label)
    return;
  label->setProperty("avatar_uid", uid);
  label->setProperty("avatar_fallback", fallback);
  if (!label->property("avatar_connected").toBool()) {
    label->setProperty("avatar_connected", true);
    connect(this, &UserMgr::sig_avatar_ready, label, [this, label](int readyUid) {
      if (label->property("avatar_uid").toInt() != readyUid)
        return;
      label->setPixmap(AvatarThumbnail(readyUid, label->property("avatar_fallback").toString(),
                                       label->size(), label->devicePixelRatioF()));
    });
  }
  label->setPixmap(AvatarThumbnail(uid, fallback, label->size(), label->devicePixelRatioF()));
}

void UserMgr::SetAvatar(int uid, const QByteArray &image)
{
  if (uid <= 0)
    return;
  ++_avatarRevisions[uid];
  QPixmap pixmap;
  if (pixmap.loadFromData(image))
    _avatars.insert(uid, pixmap);
  else
    _avatars.remove(uid);
  _avatarLoaded.insert(uid);
  emit sig_avatar_ready(uid);
}

void UserMgr::AppendApplyList(QJsonArray array)
{
  for (const auto &value : array) {
    const auto o = value.toObject();
    AddApplyList(std::make_shared<ApplyInfo>(
        o["uid"].toInt(), o["name"].toString(), o["desc"].toString(), o["icon"].toString(),
        o["nick"].toString(), o["sex"].toInt(), o["status"].toInt()));
  }
}

void UserMgr::AppendFriendList(QJsonArray array)
{
  for (const auto &value : array) {
    const auto o = value.toObject();
    auto user = std::make_shared<UserInfo>(o["uid"].toInt(), o["name"].toString(),
                                           o["nick"].toString(), o["icon"].toString(),
                                           o["sex"].toInt(), "", o["desc"].toString());
    AddFriend(user, false);
    SetFriendMetadata(user->_uid, o);
  }
}

void UserMgr::AddApplyList(std::shared_ptr<ApplyInfo> apply)
{
  if (!apply || apply->_uid <= 0)
    return;
  for (auto &old : _apply_list) {
    if (old->_uid == apply->_uid) {
      old = apply;
      return;
    }
  }
  _apply_list.push_back(apply);
}

bool UserMgr::AlreadyApply(int uid) const
{
  for (const auto &apply : _apply_list)
    if (apply->_uid == uid)
      return true;
  return false;
}

void UserMgr::MarkApplyAccepted(int uid)
{
  for (auto &apply : _apply_list)
    if (apply->_uid == uid)
      apply->_status = 1;
}

void UserMgr::RememberOutgoingApply(std::shared_ptr<SearchInfo> user)
{
  if (user && user->_uid > 0)
    _outgoing_apply_map.insert(user->_uid, user);
}

std::shared_ptr<SearchInfo> UserMgr::TakeOutgoingApply(int uid)
{
  return _outgoing_apply_map.take(uid);
}

void UserMgr::AddFriend(std::shared_ptr<UserInfo> user, bool addToFront)
{
  if (!user || user->_uid <= 0)
    return;
  // 更新现有对象，让详情页持有的 shared_ptr 同时看到新资料。
  if (_friend_map.contains(user->_uid)) {
    auto old = _friend_map.value(user->_uid);
    const QString back = old->_back;
    *old = *user;
    if (old->_back.isEmpty())
      old->_back = back;
    return;
  }
  _friend_map.insert(user->_uid, user);
  if (addToFront)
    _friend_list.insert(_friend_list.begin(), user);
  else
    _friend_list.push_back(user);
}
void UserMgr::SetFriendMetadata(int uid, const QJsonObject &metadata)
{
  const auto user = GetFriendById(uid);
  if (!user)
    return;
  if (metadata.contains("back"))
    user->_back = metadata["back"].toString();
  if (metadata["tags"].isArray()) {
    user->_tags.clear();
    for (const auto &tag : metadata["tags"].toArray()) {
      const QString text = tag.toString().trimmed();
      if (!text.isEmpty() && !user->_tags.contains(text))
        user->_tags.append(text);
    }
  }
}

void UserMgr::AddFriend(std::shared_ptr<AuthRsp> auth)
{
  AddFriend(std::make_shared<UserInfo>(auth));
}
void UserMgr::AddFriend(std::shared_ptr<AuthInfo> auth)
{
  AddFriend(std::make_shared<UserInfo>(auth));
}
std::shared_ptr<UserInfo> UserMgr::GetFriendById(int uid) const { return _friend_map.value(uid); }

int UserMgr::RemoveFriend(int uid)
{
  if (_starredFriends.remove(uid) &&
      !ContactPreferences::SaveStarred(GetUid(), gate_url_prefix, _starredFriends))
    qWarning() << "Could not remove a local starred contact preference";
  _friend_map.remove(uid);
  _friend_list.erase(
      std::remove_if(_friend_list.begin(), _friend_list.end(),
                     [uid](const std::shared_ptr<UserInfo> &user) { return user->_uid == uid; }),
      _friend_list.end());
  _apply_list.erase(
      std::remove_if(_apply_list.begin(), _apply_list.end(),
                     [uid](const std::shared_ptr<ApplyInfo> &apply) { return apply->_uid == uid; }),
      _apply_list.end());
  _outgoing_apply_map.remove(uid);
  const int threadId = _uid_to_thread_id.take(uid);
  _chat_map.remove(threadId);
  return threadId;
}

void UserMgr::AddChatThreadData(std::shared_ptr<ChatThreadData> data, int otherUid)
{
  if (!data || data->GetThreadId() <= 0)
    return;
  if (!_chat_map.contains(data->GetThreadId()))
    _chat_map.insert(data->GetThreadId(), data);
  if (otherUid > 0)
    _uid_to_thread_id.insert(otherUid, data->GetThreadId());
}
std::shared_ptr<ChatThreadData> UserMgr::GetChatThreadByThreadId(int threadId) const
{
  return _chat_map.value(threadId);
}
std::shared_ptr<ChatThreadData> UserMgr::GetChatThreadByUid(int uid) const
{
  return _chat_map.value(_uid_to_thread_id.value(uid, 0));
}
