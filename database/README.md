# 数据库建表

最后更新：2026-10-13。

| 文件 | 用途 |
| --- | --- |
| [mysql/schema.sql](mysql/schema.sql) | MySQL 8.4 服务端完整建库、建表脚本 |
| [sqlite/schema.sql](sqlite/schema.sql) | 客户端本地聊天数据库，由客户端自动初始化 |

MySQL 脚本一次建立 `cppchat` 数据库和八张表：`users`、`user_avatars`、`friend_apply`、`friend`、`chat_thread`、`private_chat`、`group_chat_member`、`chat_message`。好友标签、图片类型与大小、主键、唯一索引、分页/重试索引及外键均已包含，无需其他 SQL。

在仓库根目录打开终端，连接已安装并启动的 MySQL（账号需有建库、建表权限）：

```text
mysql --default-character-set=utf8mb4 -u root -p
```

输入密码后，在 MySQL 命令行执行：

```sql
SOURCE database/mysql/schema.sql;
SHOW TABLES;
```

也可在数据库管理工具中打开 `mysql/schema.sql` 并执行全部语句。两个聊天节点共用数据库时只需初始化一次，再将各服务 MySQL 配置指向该库。脚本不包含账号密码或演示数据。完整表结构重复导入会保留已有数据；`IF NOT EXISTS` 不会自动补齐旧表缺失的字段。

SQLite 文件由 `CppChat/rc.qrc` 打包，资源地址为 `:/storage/chatstore-schema.sql`，无需手动导入。

Redis 仍是服务端依赖。验证码、Token、节点健康租约及锁等键由业务代码写入，无需 SQL 建表文件；连接和认证由各服务配置提供。

采用 MySQL 原生 [CREATE TABLE、索引及外键](https://dev.mysql.com/doc/refman/8.4/en/create-table.html)，沿用现有 DAO 的表结构，不引入额外框架。Redis 键的创建与过期见 [SET 官方说明](https://redis.io/docs/latest/commands/set/)。
