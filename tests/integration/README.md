# 统一集成回归入口

运行 `python tests/run.py --suite full`；故障和数据库恢复运行 `--suite nightly`，说明见 [上层测试说明](../README.md)。新入口使用独立容器，不要求本机数据库管理员权限；共享连接池、共享 DAO 和 Gate DAO 都通过当轮容器执行。

下面保留的是旧的、显式使用管理 socket 的专项入口，供原有维护流程参考；统一套件不调用它，也不使用其历史结果作为当前证据。

# MySQL 连接恢复回归

在具备项目 Linux 构建依赖、MySQL CLI 及本地 MySQL 管理 socket 权限的主机上运行：

```bash
python3 tests/integration/mysql_recovery.py --source "$PWD"
```

连接池只有一份实现：`ChatServerShared/MySqlPool.cpp/.h`。测试分别验证独立共享层与 GateServer DAO，覆盖空闲超时、连接被关闭、连续重建失败、冷启动时数据库不可用、故障恢复、等待超时、关闭时唤醒等待者、日志回调异常及认证结果分类。

HTTP 回归启动独立的 Redis、StatusServer 和 GateServer：

```bash
python3 tests/integration/mysql_recovery.py --source "$PWD" \
  --gate-binary /opt/cppchat/build/bin/GateServer \
  --status-binary /opt/cppchat/build/bin/StatusServer
```

三个测试服务只监听回环地址，端口由系统动态分配，配置与随机凭据保存在权限受限的临时目录。测试使用独立的随机 MySQL 数据库和专用账号，只复制 `cppchat.users` 的表结构。fixture 用户由测试库自增分配 UID；验证码、节点健康租约和登录 Token 全部保存在本轮私有 Redis，不读取或删除现有服务的缓存。

StatusServer 使用测试自行发布的节点健康记录，验证真实的登录节点分配 RPC。这里不测试聊天 TCP 连接或发送消息；验证服务不调用邮件接口。所有测试进程由脚本自行启动和回收，不按端口或名称终止现有进程。

HTTP 用例验证 `1007`（用户名/邮箱不匹配）、`1009`（密码错误）、`1014`（数据库故障）、恢复后重置和登录成功、旧密码失效以及验证码只能使用一次。请求调用与检查使用显式函数，`python3 -O` 和 `PYTHONOPTIMIZE` 不会跳过用例或错误检查；失败时进程退出码非零。

普通模式与优化模式可同时运行，用于验证资源隔离。源码中的构建命令使用 Linux 已安装的系统库，测试产物只写入临时目录，业务服务工程不编入测试入口。
