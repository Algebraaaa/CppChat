# 数据库脚本

数据库初始化和迁移脚本统一放在此目录，与各组件业务源码分开维护。

| 路径 | 用途 |
| --- | --- |
| `mysql/init_cppchat.sql` | MySQL 数据库及表结构初始化，脚本选择 `cppchat` 数据库 |
| `mysql/migrations/2026-09-29-chat-message-media.sql` | 旧聊天消息表增加图片类型及大小字段 |
| `mysql/migrations/season2_image_migration.sql` | 另一份历史图片字段迁移，新增字段与上面的脚本相同 |
| `mysql/migrations/2026-10-02-chat-message-retry-index.sql` | 消息重试查询索引，可重复执行 |
| `mysql/migrations/2026-10-02-contact-tags.sql` | 好友和好友申请标签字段，可重复执行 |
| `sqlite/chatstore-schema.sql` | Qt 客户端本地聊天数据库的初始表结构 |

MySQL 迁移针对服务端配置的数据库执行。ChatServer 和 ChatServer2 共用同一数据库时只需执行一次；使用不同数据库时分别执行。原来两个节点中内容相同的脚本已合并为一份。

两份历史图片迁移均直接新增 `msg_type` 和 `media_size`，不支持重复执行。旧库缺少这两个字段时按需选择其中一份；新初始化的数据库已包含这些字段，不需要执行。不要把整个迁移目录无条件执行一遍。

SQLite 建表脚本由 `CppChat/rc.qrc` 打包进客户端，资源路径仍为 `:/storage/chatstore-schema.sql`，运行时无需读取仓库目录。构建客户端时需要同时保留本目录。
