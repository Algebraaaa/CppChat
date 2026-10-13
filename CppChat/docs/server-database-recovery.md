# 服务端数据库连接恢复

2026-10-12 修复：MySQL 在连接空闲超过 `wait_timeout` 后关闭会话，连接池中的 C++ 对象仍可能存在。此前 GateServer 直接借出旧连接，查询时报 `4031`（空闲超时）或 `2013`（连接断开）；DAO 把异常返回为 `false`，造成密码重置显示“用户名与邮箱不匹配”，登录显示“邮箱或密码错误”。

## 职责与工程

GateServer 和两个聊天节点共用 `ChatServerShared/MySqlPool.cpp/.h`，各进程仍独立持有连接和运行状态。GateServer 的 `MysqlDao` 不再定义第二套连接池，CMake 明确登记公共实现。连接创建、检查和恢复在连接池内部完成，不额外暴露健康检查工具头文件。

连接池不依赖服务配置管理器、HTTP 协议或服务日志类。配置层传入 `MySqlPoolOptions` 与错误日志回调，回调异常不会影响恢复。DAO 返回 `AccountCheckResult`、`PasswordUpdateResult` 等数据访问结果，HTTP 层负责映射协议错误码：真实账号不匹配为 `1007`，真实密码错误为 `1009`，连接不可用或 SQL 异常为 `1014`。客户端已有 `1014` 的提示，无需变更协议。

## 恢复与超时

每次借出连接前执行 `SELECT 1`；连接失效时创建新会话并恢复默认数据库。网络操作在队列锁之外执行。启动时连接失败以及运行时重建失败，都保留槽位供后续请求重试，避免永久丢失池容量。

连接器关闭隐式自动重连，重建只发生在借出前，不自动重放写入或进行中的事务，避免丢失事务和行锁状态后继续写入。

各服务 `[Mysql]` 可配置以下超时（秒）：

```ini
BorrowTimeoutSeconds=3
ConnectTimeoutSeconds=3
ReadTimeoutSeconds=5
WriteTimeoutSeconds=5
```

未配置时保持这些默认值，有效策略范围为 1–300 秒。连接池分别限制借用等待、连接和读写；这不是整个业务请求的统一截止时间，也不保证取消正在执行的 SQL。

网关可选 `[GateServer] Host` 指定数字形式的监听地址，默认 `0.0.0.0` 保持现有行为。隔离测试使用 `127.0.0.1` 与动态端口。

## 规范与验证

根目录 `.clang-format` 定义公共代码格式，GateServer 的 `.clang-format` 保留该组件的四空格与 Allman 大括号风格。只格式化并检查本轮修改的 C++ 文件，其他历史源码不作为此次已通过规范检查的范围。

验证方法见 [MySQL 集成回归](../../tests/integration/README.md)。测试数据库、账号、服务进程和 Redis 全部按运行隔离；请求和校验不使用会被 Python 优化模式删除的 `assert`。测试不修改现有账号密码或业务表结构。

本轮 Windows Debug 与云端 Linux Release 的 GateServer、ChatServer、ChatServer2 均编译通过；所修改 C++ 文件通过 `clang-format --dry-run --Werror`，Python 脚本通过语法检查。普通模式和 `python -O` 的完整回归并行通过，另外确认优化模式下显式失败仍返回非零退出码。

新版云端服务已重启生效，公网 HTTPS 网关验证通过。临时数据库和测试账号已全部清理，业务库仍有 32 个账号；本轮没有修改这些账号的密码。
