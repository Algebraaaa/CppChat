#ifndef GLOBAL_H
#define GLOBAL_H

#include <QStyle>
#include <QString>
#include <QWidget>

/**
 * @brief repolish 用来刷新qss
 */
inline void repolish(QWidget *widget)
{
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
}

enum ReqId {
  ID_GET_VERIFY_CODE = 1001, // 获取验证码
  ID_REG_USER = 1002,        // 注册用户
  ID_RESET_PWD = 1003,       // 重置密码
  ID_LOGIN_USER = 1004,      // 用户登录
  ID_CHAT_LOGIN = 1005,      // 登录聊天服务器
  ID_CHAT_LOGIN_RSP = 1006   // 登录聊天服务器回包
};
enum Modules { REGISTERMOD = 0, RESETMOD = 1, LOGINMOD = 2 };
enum ErrorCodes {
  SUCCESS = 0,
  ERR_JSON = 1,    // json解释失败
  ERR_NETWORK = 2, // 网络错误
};
enum class TipError {
  User,
  Email,
  Password,
  NewPassword,
  ConfirmPassword,
  VerifyCode,
  TIP_EMAIL_ERR,
  TIP_PWD_ERR
};
struct ServerInfo {
  QString Host;
  QString Port;
  QString Token;
  int Uid;
};
enum ChatUIMode {
  SearchMode,  // 搜索模式
  ChatMode,    // 聊天模式
  ContactMode, // 联系模式
};
// 自定义QListWidgetItem的几种类型
enum ListItemType {
  CHAT_USER_ITEM,    // 聊天用户
  CONTACT_USER_ITEM, // 联系人用户
  SEARCH_USER_ITEM,  // 搜索到的用户
  ADD_USER_TIP_ITEM, // 提示添加用户
  INVALID_ITEM,      // 不可点击条目
  GROUP_TIP_ITEM,    // 分组提示条目
  LINE_ITEM,         // 分割线
  APPLY_FRIEND_ITEM, // 好友申请
};
enum class ChatRole {

  Self,
  Other
};
struct MsgInfo {
  QString msgFlag; //"text,image,file"
  QString content; // 表示文件和图像的url,文本信息
  QPixmap pixmap;  // 文件和图片的缩略图
};
// 申请好友标签输入框最低长度
const int MIN_APPLY_LABEL_ED_LEN = 40;

const QString add_prefix = "添加标签 ";

const int tip_offset = 5;

inline QString gate_url_prefix;

#endif // GLOBAL_H
