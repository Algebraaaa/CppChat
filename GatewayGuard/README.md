# GatewayGuard

从已有云端运行源码取回的 HTTP 安全网关，独立于 C++ GateServer，不改注册、登录、
验证码和重置密码的业务协议。只监听回环地址，公网入口由 Nginx TLS 代理。
本轮只归入统一源码版本，不重新部署或更改正在运行的网关。

职责：请求大小限制、Redis 原子限流、账号失败次数限制、上游超时与响应大小限制。
日志不输出正文、邮箱、密码、验证码或 Token。只有回环连接提供正确代理密钥时才接受
`X-Real-IP`；不要直接信任公网请求的转发 IP。

## 依赖与配置

沿用 `VerifyServer/package-lock.json` 锁定的 `ioredis` 5.8.0，不再维护另一份依赖树。
先在 `VerifyServer` 中使用 `npm ci --ignore-scripts` 安装锁定依赖。
网关默认 `require('ioredis')`；独立部署时用 `CPPCHAT_REDIS_MODULE` 指向已有模块目录。

| 环境变量 | 含义 |
| --- | --- |
| `CPPCHAT_VERIFY_CONFIG` | 必填，私有 VerifyServer JSON 配置路径，仅读取其中 `redis` 配置 |
| `CPPCHAT_REDIS_MODULE` | 可选，`ioredis` 模块的绝对目录 |
| `CPPCHAT_GUARD_PORT` | 网关回环监听端口，默认 8090 |
| `CPPCHAT_GATE_PORT` | C++ GateServer 回环端口，默认 18080 |
| `CPPCHAT_PROXY_SECRET_FILE` | 可选，Nginx 与网关共享的私有密钥文件，至少 32 字符 |
| `CPPCHAT_GUARD_KEY_PREFIX` | Redis 限流键前缀，默认 `security:`；测试必须使用隔离前缀 |

端口必须是 1～65535 的整数。运行入口为 `node server.js`，配置信息由外部环境提供。
不要把 Redis 密码、SMTP 授权码、代理密钥、TLS 私钥或云端运行配置提交到仓库。
不设置代理密钥时只使用 socket 地址限流，不信任转发头。

仅语法检查，不启动服务：`node --check GatewayGuard/server.js`。
此次同步保留云端源码原样，不将语法检查表述为真实限流、Redis 或 TLS 的运行验证。

## 三方同步

完整 Git 仓库与实际运行目录分别管理。`/opt/cppchat/repository` 保存统一提交的仓库，
`/opt/cppchat/source` 保存该提交的源码导出；正在运行的网关仍位于
`/opt/cppchat/runtime/GatewayGuard`。源码同步不会自动重启或替换运行服务。
Linux 维护入口见 [云端说明](../deploy/linux/README.md)。
