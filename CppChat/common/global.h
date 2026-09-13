#ifndef GLOBAL_H
#define GLOBAL_H

#include <QString>
#include <QPixmap>
#include <QStyle>
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
  // 1001～1004是客户端 HTTP 回调标签，不会发送给服务端
  // 其中客户端和服务端需要一致的是：
  // - URL：/get_verifycode、/user_register、/reset_pwd、/user_login
  // - 请求方法：POST、GET
  // - JSON 字段名称和类型
  // - 返回 JSON 的字段和错误码含义（辰哥的客户端错误码很笼统，我这里改的具体了）
  ID_GET_VERIFY_CODE = 1001, // 获取验证码
  ID_REG_USER = 1002,        // 注册用户
  ID_RESET_PWD = 1003,       // 重置密码
  ID_LOGIN_USER = 1004,      // 用户登录
  ID_CHAT_LOGIN = 1005,      // 登陆聊天服务器
  // 1005 以后会写入 TCP 包头，发送到服务端
  // 两端必须统一的有：消息编号（枚举名称可以不同，客户端与服务端的线上真正传输的只有数值）、
  // 包头长度、大小端、JSON 格式
  ID_CHAT_LOGIN_RSP = 1006,           // 登陆聊天服务器回包
  ID_SEARCH_USER_REQ = 1007,          // 用户搜索请求
  ID_SEARCH_USER_RSP = 1008,          // 搜索用户回包
  ID_ADD_FRIEND_REQ = 1009,           // 添加好友申请
  ID_ADD_FRIEND_RSP = 1010,           // 申请添加好友回复
  ID_NOTIFY_ADD_FRIEND_REQ = 1011,    // 通知用户添加好友申请
  ID_AUTH_FRIEND_REQ = 1013,          // 认证好友请求
  ID_AUTH_FRIEND_RSP = 1014,          // 认证好友回复
  ID_NOTIFY_AUTH_FRIEND_REQ = 1015,   // 通知用户认证好友申请
  ID_TEXT_CHAT_MSG_REQ = 1017,        // 文本聊天信息请求
  ID_TEXT_CHAT_MSG_RSP = 1018,        // 文本聊天信息回复
  ID_NOTIFY_TEXT_CHAT_MSG_REQ = 1019, // 通知用户文本聊天信息
  ID_NOTIFY_OFF_LINE_REQ = 1021,      // 通知用户下线
  ID_HEART_BEAT_REQ = 1023,           // 心跳请求
  ID_HEARTBEAT_RSP = 1024,            // 心跳回复
  ID_LOAD_CHAT_THREAD_REQ = 1025,     // 加载聊天线程
  ID_LOAD_CHAT_THREAD_RSP = 1026,     // 加载聊天线程回复
  ID_CREATE_PRIVATE_CHAT_REQ = 1027,  // 创建私聊请求
  ID_CREATE_PRIVATE_CHAT_RSP = 1028,  // 创建私聊回复
  ID_LOAD_CHAT_MSG_REQ = 1029,        // 加载聊天消息
  ID_LOAD_CHAT_MSG_RSP = 1030,        // 加载聊天消息
};
enum Modules { REGISTERMOD = 0, RESETMOD = 1, LOGINMOD = 2 };
enum ErrorCodes {
  // 客户端本地网络错误，不会由服务端通过协议返回
  NetworkError = -1,
  Success = 0,
  Error_Json = 1001,
  RPCFailed = 1002,
  VerifyExpired = 1003,
  VerifyCodeErr = 1004,
  UserExist = 1005,
  PasswdErr = 1006,
  EmailNotMatch = 1007,
  PasswdUpFailed = 1008,
  PasswdInvalid = 1009,
  TokenInvalid = 1010,
  UidInvalid = 1011,
  CreateChatFailed = 1012,
  LoadChatFailed = 1013,
  DatabaseError = 1014,
  RedisError = 1015,
  InternalError = 1016,
};

// 根据错误类型输出对应的错误信息
inline QString errorCodeMessage(int errorCode)
{
  switch (errorCode) {
  case ErrorCodes::NetworkError:
    return QStringLiteral("网络连接失败，请检查服务是否启动");
  case ErrorCodes::Success:
    return QStringLiteral("操作成功");
  case ErrorCodes::Error_Json:
    return QStringLiteral("请求或响应数据格式错误");
  case ErrorCodes::RPCFailed:
    return QStringLiteral("后端服务暂时不可用，请稍后重试");
  case ErrorCodes::VerifyExpired:
    return QStringLiteral("验证码已过期，请重新获取");
  case ErrorCodes::VerifyCodeErr:
    return QStringLiteral("验证码错误，请重新输入");
  case ErrorCodes::UserExist:
    return QStringLiteral("用户名或邮箱已存在");
  case ErrorCodes::PasswdErr:
    return QStringLiteral("两次输入的密码不一致");
  case ErrorCodes::EmailNotMatch:
    return QStringLiteral("用户名与邮箱不匹配");
  case ErrorCodes::PasswdUpFailed:
    return QStringLiteral("密码更新失败，请稍后重试");
  case ErrorCodes::PasswdInvalid:
    return QStringLiteral("邮箱或密码错误");
  case ErrorCodes::TokenInvalid:
    return QStringLiteral("登录凭证无效，请重新登录");
  case ErrorCodes::UidInvalid:
    return QStringLiteral("用户身份无效，请重新登录");
  case ErrorCodes::CreateChatFailed:
    return QStringLiteral("创建聊天会话失败");
  case ErrorCodes::LoadChatFailed:
    return QStringLiteral("加载聊天记录失败");
  case ErrorCodes::DatabaseError:
    return QStringLiteral("数据库服务异常，请稍后重试");
  case ErrorCodes::RedisError:
    return QStringLiteral("验证码存储服务异常，请稍后重试");
  case ErrorCodes::InternalError:
    return QStringLiteral("服务器内部错误，请稍后重试");
  default:
    return QStringLiteral("未知错误（错误码：%1）").arg(errorCode);
  }
}

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
enum ClickLbState { Normal = 0, Selected = 1 };

// 申请好友标签输入框最低长度
const int MIN_APPLY_LABEL_ED_LEN = 40;

const QString add_prefix = "添加标签 ";

const int tip_offset = 5;

inline QString gate_url_prefix;

// 协议状态沿用参考项目；服务端回执只代表服务器已接收，不能据此声称对方已读。
enum MsgStatus { UN_READ = 0, SEND_FAILED = 1, READED = 2 };
enum class ChatFormType { PRIVATE = 0, GROUP = 1 };
enum class ChatMsgType { TEXT = 0, PIC = 1, FILE = 2 };
inline constexpr int CHAT_COUNT_PER_PAGE = 13;

#endif // GLOBAL_H
