# 版本发布与 CD

`main` 和 PR 只验证；推送版本标签触发 Windows/Ubuntu 构建测试、GitHub Release 和云端部署。

```powershell
git pull --ff-only
# 完成本地测试、提交并推送 main 后，选择高于线上版本的标签。
git tag -a v0.1.6 -m "CppChat 0.1.6"
git push origin v0.1.6
```

Windows 使用 Qt 6.12.0 / MinGW，运行客户端、更新、安全和安装/升级/卸载验证。
Ubuntu 24.04 构建服务端，运行核心及隔离 MySQL/Redis/SMTP/跨节点回归；不会测试真实账号。
两边通过后 Release 发布 EXE、Windows 更新 JSON、Linux 归档和两份 SHA256 文件，版本化附件不覆盖。
目前没有签名证书，安装包明确标为未签名，哈希校验不能代替代码签名。

部署通过 `production` Environment 的独立 SSH 身份进行，sudo 仅允许调用管理员安装的固定
控制器。控制器验证路径、SHA256、ELF 平台和 Git 源码，备份并更新程序和 Windows 更新入口；
保留数据库、Redis 数据、环境配置、SMTP 实现和证书，不执行建库或迁移 SQL。
健康检查失败恢复旧程序、模块、源码导出和更新清单。数据库变更需单独备份和迁移。

## Secrets 与目录

仓库 Secret `CPPCHAT_CLIENT_CONFIG` 是客户端公开 HTTPS/TLS 配置。连接地址会进入安装包；
不得包含管理账号、服务器密钥、数据库或邮件密码。
`production` Environment 只允许 `v*` 标签，使用 Secrets `DEPLOY_HOST`、`DEPLOY_PORT`、
`DEPLOY_USER`、`DEPLOY_SSH_KEY` 和 `DEPLOY_KNOWN_HOSTS`。私钥属于独立部署账户，
不复用日常管理员密钥；SSH 主机公钥须核对，禁止关闭校验。

实际值及首次配置记录只保存在忽略的 `.runtime/github-cd/`。固定控制器位于
`/opt/cppchat/deploy/cd/`；修改控制器需要管理员更新，上传目录里的任意脚本不会被 sudo 执行。
发布备份为 `/opt/cppchat/backups/cd-*`，当前版本及程序指纹在 `/opt/cppchat/source-version.json`。
Actions 可重跑失败 job；完整重跑同一版本产生不同附件时会拒绝覆盖，需递增版本。

## 来源与验证范围

复用 [GitHub Release CLI](https://cli.github.com/manual/gh_release_create)、
[GitHub Environments](https://docs.github.com/en/actions/how-tos/deploy/configure-and-manage-deployments/manage-environments)、
[install-qt-action](https://github.com/jurplel/install-qt-action) 和已有 CMake、CTest、pytest、
Inno Setup、安装验证及更新发布事务。Actions 固定完整提交；安装 Action 为 4.4.1（MIT），
aqtinstall 固定 `076e1659807d0b362a3ed684d54c2e9c775eb9c7`，已核对其 Qt 6.12 按编译器分层仓库解析。
Inno Setup 6.7.3 从上游下载并验证签名。不复制或修改第三方下载器源码。
草稿 Release 按 [GitHub Release API](https://docs.github.com/en/rest/releases/releases#list-releases)
使用有推送权限的分页列表读取；标签查询仅用于已公开的 Release，避免草稿被误判为不存在。
创建或公开后的查询采用有上限的等待重试，适应 GitHub 可见性延迟；SSH 密钥和主机公钥
写入 runner 时统一为 LF，兼容从 Windows 设置的 Secrets。

新增脚本只编排版本、归档校验和环境激活，聊天、数据库及 Qt 部署沿用原模块。
`python -B -m unittest discover -s tests/deployment` 验证损坏文件、身份、路径穿越、
链接及多余文件在激活前被拒绝；`tests/packaging` 验证更新发布事务。
工具、诊断和失败证据放入忽略的 `build/`，日志和真实配置不作为 Release 附件。
