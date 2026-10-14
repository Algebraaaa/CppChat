# Linux 云端源码与运行目录

从现有云端维护说明和构建脚本整理；不包含运行凭据、业务数据、证书或安装包。
2026-10-14 统一以最新 GitHub 源码为基线，合入 Windows 气泡修复、云端头像更新权限修复，
并取回此前仅在云端存在的 GatewayGuard 和单任务构建入口。

## 目录边界

| 路径 | 用途 |
| --- | --- |
| `/opt/cppchat/repository` | GitHub 统一版本的完整 Git checkout，含客户端、服务端、数据库和测试源码 |
| `/opt/cppchat/source` | 同一 Git 提交的源码导出，云端 CMake 构建从这里读取 |
| `/opt/cppchat/source-version.json` | 本次同步的提交号和源码同步说明，不代表运行二进制已重新构建 |
| `/opt/cppchat/build/bin` | 现有服务端二进制 |
| `/opt/cppchat/deploy/build.sh` | 当前云端单任务构建入口；版本化副本为本目录 `build.sh` |
| `/opt/cppchat/runtime/<服务名>` | 私有运行配置、依赖和日志，与 Git 源码分开 |
| `/opt/cppchat/runtime/resource_data` | 上传图片与元数据，不能由源码同步覆盖 |
| `/opt/cppchat/runtime/redis` | Redis 配置与持久化数据 |
| `/var/lib/mysql` | MySQL 数据，不能由源码同步覆盖 |

Node 服务更新必须保留私有配置及已有依赖。验证码服务的 SMTP/Redis 配置位于
`/opt/cppchat/runtime/VerifyServer/config.json`；代理密钥独立于安全网关源码保存。
凭据归档、配置内容、证书、私钥和上传安装包均不进入公开仓库。

## 连接与维护

客户端使用 HTTPS 网关和 TLS 聊天/图片传输，校验服务器证书；聊天节点由登录接口分配。
Nginx 将公网连接转到内部业务端口，MySQL、Redis 和 gRPC 保持回环访问。
具体域名/IP、端口和证书路径由各环境的私有配置决定。

```bash
systemctl status cppchat-GateServer cppchat-GatewayGuard cppchat-VerifyServer cppchat-StatusServer cppchat-ChatServer cppchat-ChatServer2 cppchat-ResourceServer --no-pager
```

服务 active 只证明进程状态，不等于注册、邮件、登录和图文链路均已验收。
本次同步不重新构建、不修改数据库、不替换运行二进制，也不重启服务。
需要构建时由维护者明确执行 `bash /opt/cppchat/deploy/build.sh`；该脚本不负责重启。

## 同步与回滚

以 Git 提交号为唯一源码版本。源码同步前保存旧 `/opt/cppchat/source` 的归档，
从同一个提交导出到 source，并核对全部受版本管理的文件；不要用生成时间代替版本证据。
使用 [Git fast-forward](https://git-scm.com/docs/git-merge) 保留历史，
从 [git archive](https://git-scm.com/docs/git-archive) 生成同一提交的源码包；不强制推送。
运行二进制未重新构建时必须与源码版本状态分别记录。

本机旧的多仓库工作区保持原样，源码 ZIP、改动补丁、云端原始源码与维护说明归档在
统一本机仓库的 `build/sync/20261014/`，此目录被 Git 忽略。原配置仍留在原目录。
29 处云端格式/IDE 分组差异沿用 GitHub 基线；云端 `UpdateProfile` 的实际权限修复保留：
`user_avatars` 由 `database/mysql/init_cppchat.sql` 初始化，业务写入不再执行建表 DDL。
