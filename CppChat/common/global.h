#ifndef GLOBAL_H
#define GLOBAL_H

#include <QString>
#include <QStyle>
#include <QWidget>
#include <array>
/**
 * @brief repolish 用来刷新qss
 */
inline void repolish(QWidget *widget)
{
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
}

enum ReqId {
  ID_GET_VERIFY_CODE = 1001,          // 获取验证码
  ID_REG_USER = 1002,                 // 注册用户
  ID_RESET_PWD = 1003,                // 重置密码
  ID_LOGIN_USER = 1004,               // 用户登录
  ID_CHAT_LOGIN = 1005,               // 登陆聊天服务器
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
struct ChatUserTestData {
  QString displayName;
  QString avatarResource;
  QString lastMessage;
};

// 聊天主页的固定测试数据。
// 每条记录将用户名、头像和最后一条消息绑定在一起，确保每次启动的测试结果一致。
const std::array<ChatUserTestData, 33> kChatUserTestData = { {
  { QStringLiteral("林夏"), QStringLiteral(":/res/head_1.jpg"),
    QStringLiteral("今晚一起调试客户端吗？") },
  { QStringLiteral("周屿"), QStringLiteral(":/res/head_2.jpg"),
    QStringLiteral("需求文档已经更新了。") },
  { QStringLiteral("Alex"), QStringLiteral(":/res/head_3.jpg"),
    QStringLiteral("The latest build is ready.") },
  { QStringLiteral("Mia"), QStringLiteral(":/res/head_4.jpg"),
    QStringLiteral("明天下午三点开会。") },
  { QStringLiteral("Chen"), QStringLiteral(":/res/head_5.jpg"),
    QStringLiteral("接口联调通过了。") },
  { QStringLiteral("Aurora"), QStringLiteral(":/res/head_6.jpg"),
    QStringLiteral("欢迎来到 CppChat！") },
  { QStringLiteral("Nova"), QStringLiteral(":/res/head_7.jpg"),
    QStringLiteral("头像资源加载正常。") },
  { QStringLiteral("Willow"), QStringLiteral(":/res/head_8.jpg"),
    QStringLiteral("搜索功能很好用。") },
  { QStringLiteral("Felix"), QStringLiteral(":/res/head_9.jpg"),
    QStringLiteral("稍后把日志发给你。") },
  { QStringLiteral("Leo"), QStringLiteral(":/res/head_10.jpg"),
    QStringLiteral("今天的任务完成啦。") },
  { QStringLiteral("Rust Team"), QStringLiteral(":/res/head_6.jpg"),
    QStringLiteral("消息列表滚动测试。") },
  { QStringLiteral("Qt Group"), QStringLiteral(":/res/head_7.jpg"),
    QStringLiteral("QSS 样式已经更新。") },
  { QStringLiteral("Backend"), QStringLiteral(":/res/head_8.jpg"),
    QStringLiteral("ChatServer is online.") },
  { QStringLiteral("Luna"), QStringLiteral(":/res/head_11.jpg"),
    QStringLiteral("今晚的月色真好。") },
  { QStringLiteral("Iris"), QStringLiteral(":/res/head_12.jpg"),
    QStringLiteral("新的交互稿已经发你了。") },
  { QStringLiteral("Owen"), QStringLiteral(":/res/head_13.jpg"),
    QStringLiteral("我正在检查消息协议。") },
  { QStringLiteral("Hazel"), QStringLiteral(":/res/head_14.jpg"),
    QStringLiteral("周末一起喝咖啡吗？") },
  { QStringLiteral("Milo"), QStringLiteral(":/res/head_15.jpg"),
    QStringLiteral("客户端启动速度不错。") },
  { QStringLiteral("Ruby"), QStringLiteral(":/res/head_16.jpg"),
    QStringLiteral("测试用例已经补齐。") },
  { QStringLiteral("Theo"), QStringLiteral(":/res/head_17.jpg"),
    QStringLiteral("TCP 连接保持正常。") },
  { QStringLiteral("Chloe"), QStringLiteral(":/res/head_18.jpg"),
    QStringLiteral("收到，稍后回复你。") },
  { QStringLiteral("Jasper"), QStringLiteral(":/res/head_19.jpg"),
    QStringLiteral("代码审查已经完成。") },
  { QStringLiteral("Zoe"), QStringLiteral(":/res/head_20.jpg"), QStringLiteral("明天见，晚安！") },
  { QStringLiteral("Akari"), QStringLiteral(":/res/head_21.jpg"),
    QStringLiteral("新番更新啦，一起看吗？") },
  { QStringLiteral("Hikari"), QStringLiteral(":/res/head_22.jpg"),
    QStringLiteral("今天也要元气满满！") },
  { QStringLiteral("Ren"), QStringLiteral(":/res/head_23.jpg"),
    QStringLiteral("这个表情包太可爱了。") },
  { QStringLiteral("Sora"), QStringLiteral(":/res/head_24.jpg"),
    QStringLiteral("组队任务还差一个人。") },
  { QStringLiteral("Yuki"), QStringLiteral(":/res/head_25.jpg"),
    QStringLiteral("漫画看到最新一话了吗？") },
  { QStringLiteral("Rin"), QStringLiteral(":/res/head_26.jpg"),
    QStringLiteral("头像切换测试通过。") },
  { QStringLiteral("Kaito"), QStringLiteral(":/res/head_27.jpg"),
    QStringLiteral("晚上八点准时上线。") },
  { QStringLiteral("Mei"), QStringLiteral(":/res/head_28.jpg"),
    QStringLiteral("今天画了一张新立绘。") },
  { QStringLiteral("Haru"), QStringLiteral(":/res/head_29.jpg"),
    QStringLiteral("活动奖励已经领取。") },
  { QStringLiteral("Aoi"), QStringLiteral(":/res/head_30.jpg"), QStringLiteral("下次漫展见！") },
} };
inline std::vector<QString> heads = { ":/res/head_1.jpg", ":/res/head_2.jpg", ":/res/head_3.jpg",
                                      ":/res/head_4.jpg", ":/res/head_5.jpg" };
inline std::vector<QString> strs = { "你好，在吗？",
                                     "今天晚上一起吃饭吗？",
                                     "这个功能已经完成了",
                                     "Qt6 的界面看起来还不错",
                                     "测试一下消息列表显示效果",
                                     "这是一条比较长的消息，用来测试文本自动换行和控件高度变化" };

inline std::vector<QString> names = { "张三", "李四", "王五", "赵六", "Alice", "Bob" };
enum ClickLbState { Normal = 0, Selected = 1 };

// 申请好友标签输入框最低长度
const int MIN_APPLY_LABEL_ED_LEN = 40;

const QString add_prefix = "添加标签 ";

const int tip_offset = 5;

inline QString gate_url_prefix;

#endif // GLOBAL_H
