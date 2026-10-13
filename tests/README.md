# CppChat 回归测试

统一入口：`python tests/run.py --suite smoke|full|nightly|release`。业务构建默认关闭测试；测试不会读取 `.runtime`、开发配置或生产数据。所有 SQL 仍在 `database/`，测试只引用脚本。

首次准备 Python 3.12+ 虚拟环境和锁定依赖：

```powershell
python -m venv build/test-env
build/test-env/Scripts/python -m pip install -r tests/requirements.lock
npm ci --prefix VerifyServer --ignore-scripts
```

Linux 对应 `build/test-env/bin/python`。入口自动使用项目虚拟环境。未安装依赖或 Docker 未运行会生成 BLOCKED 报告；不会自动操作本机数据库。Node 直接依赖固定版本，完整依赖在 `VerifyServer/package-lock.json`；GoogleTest/GoogleMock 固定到 v1.17.0 发布提交，仅测试构建下载。

Windows 首次配置两个独立构建目录：服务端沿用 MSVC/vcpkg 工具链，客户端沿用自己的 Qt Kit。已配置工程可直接运行入口；新环境先向 CMake 提供对应工具链/Qt 路径。客户端支持 `--config <file> --data-dir <directory> --log-dir <directory>`，测试覆盖数据、附件、会话偏好、表情和日志路径；指定数据目录时不迁移日常使用的旧库。

```powershell
python tests/run.py --suite smoke
python tests/run.py --suite full
python tests/run.py --suite nightly
python tests/run.py --suite release --configuration Release --qt-build CppChat/build/release --manual-results build/manual-results.json
```

`--no-build` 仅复用已启用 `CPPCHAT_BUILD_TESTS=ON` 的构建。可通过 `--server-build`、`--qt-build`、`--cmake`、`--qt-cmake` 指定位置和同一 Kit 的工具。CTest 拒绝空测试集合；入口不以历史 XML 替代当前执行结果。

每轮报告在 `build/regression/<时间-随机ID>/`：总览 `index.html`、`summary.json`、`combined.xml`，两种 Python 模式各自的 pytest HTML/JUnit，GoogleTest/Qt JUnit，日志、截图、随机种子和依赖信息。默认保留最新三轮完整目录，旧报告压缩到 `build/regression/archive/`，校验归档完整后才删除展开目录，失败证据也保留。`--keep-runs N` 调整展开数量；未完成运行和无法识别的目录不自动清理。退出码 0=PASS，1=FAIL，2=BLOCKED。跳过必测项按 BLOCKED 统计；占位功能实现状态为“未实现”，其当前交互仍需要通过。测试不自动重跑失败用例。

文件按用途归类，运行产物均不提交：

| 位置 | 内容与处理方式 |
|---|---|
| `tests/unit`、`cpp`、`qt`、`node` | 单元及组件测试源码，保留 |
| `tests/integration`、`regression` | 真实服务功能、故障及验收测试源码，保留 |
| `tests/harness`、`ci`、`performance`、`manual` | 隔离环境、CI、压测及人工验收工具，保留 |
| `database/` | 初始化和迁移 SQL，独立于业务源码 |
| `build/regression/` | 最新报告、历史归档、当轮日志与截图 |
| `build/test-cache/` | pytest、Ruff 可再生缓存，可删除 |
| `build/test-env/`、`VerifyServer/node_modules/` | 已安装测试及运行依赖，保留以复用 |
| `build/server/`、`CppChat/build/` | 日常开发构建，保留以复用 |

诊断时请将日志和临时构建放入 `build/`，不要放在源码目录。一次性验证构建用完可删除；删除后再次运行对应 Linux/覆盖率任务需要重新构建。统一入口不在 `tests/` 写 Python 字节码或 pytest 缓存。

套件内容：

| 套件 | 自动范围 | 必须另行记录的证据 |
|---|---|---|
| Smoke | 两套本机测试、验证码处理器、核心真实 SMTP/认证/跨节点图文/离线/重置/删除链路、协议边界 | 无 |
| Full | 全部非故障自动用例、数据库兼容、Qt/HTTP TLS、启动配置 | 桌面人工部分仍在报告中列为待验收 |
| Nightly | Full + Hypothesis、并发重置、MySQL 过期/KILL/冷启动/恢复、Toxiproxy 故障 | 长时性能测量使用下述入口；ASan/UBSan/TSan 及覆盖率由独立 CI 构建执行 |
| Release | Debug/Release 构建及本机测试、全部自动回归 + Windows 人工清单 | Debug 和 Release 两种构建、发布包与真实桌面证据；缺少即 BLOCKED |

`cases.json` 映射自动及人工用例，每项记录前置、输入、响应、数据变化、清理方法。人工步骤在 [manual/windows.md](manual/windows.md)，运行 `python tests/manual/prepare.py --output build/manual-results.json` 生成待验收记录。需要验收人、时间、Kit、编译器、DPI、非空证据，以及相同源码指纹；历史文档中的“通过”不被接受。

真实依赖采用 Testcontainers：MySQL 8.4.0、Redis 7.4.2、Mailpit 1.21.8、Toxiproxy 2.9.0。每个 Python 模式独立容器/随机账号/动态端口，启动五个 C++ 二进制和一个 Node 进程。故障用例另外独立运行，避免污染普通用例连接池。失败、正常退出都会回收自己创建的进程和容器；请勿用系统进程名批量结束服务。容器端口不使用本机 MySQL 3306。初始化已含媒体列，只执行标签和重试索引适用迁移，并验证重复初始化及旧 users 表保留。

TLS 使用当轮 CA、私钥、有效/过期/不可信证书和回环代理；Qt probe 使用同一 Kit 的 QSslSocket，严格验证主机名，不调用 `ignoreSslErrors()`。私钥是运行产物。截图计算覆盖四种缩放，真实多屏、焦点、系统剪贴板和发布依赖必须人工验收。

Linux 构建直接使用根 CMake 和版本控制内源码。可用 `tests/ci/Dockerfile` 复现依赖环境；`bash tests/ci/build-linux.sh` 构建服务端和 Qt。CI 在 PR 运行 Smoke、push 运行 Full、夜间运行随机/故障，单独运行 ASan、UBSan、适用的 TSan；覆盖率使用单独 `CPPCHAT_COVERAGE=ON` 构建并执行 80% 行/70% 分支门槛，不将尚未达到门槛的报告记为通过。

性能：

```powershell
build/test-env/Scripts/python tests/performance/run.py --profile baseline --server-build build/server --artifacts build/performance
build/test-env/Scripts/python tests/performance/run.py --profile soak --server-build build/server --artifacts build/soak
python tests/run.py --suite nightly --performance baseline --approved-baseline <approved.json>
```

默认 100 连接、每连接 1 条/秒、600 秒，或 50 连接、7200 秒，三次取中位数。k6 测 HTTP，asyncio 复用功能编解码器测真实双节点聊天；检查每条回执、送达和唯一落库，记录吞吐、P95/P99、内存、线程、句柄/FD、在途积压，保存服务端队列指标日志。`--connections/--duration/--runs` 仅诊断，不可冒充约定基线。没有已批准的同环境基线时输出候选并标记比较 BLOCKED；提供带 `approved: true`、`approved_by` 的同环境记录后，P95 超过 20% 退化判 FAIL。每周两小时稳定性在独立长时 CI 作业执行，不占 PR。

`--resource-every N` 可在同一负载中每 N 秒为每对账号增加一次 32 KiB 资源上传及接收方下载；独立记录资源 P95/P99 并校验 SHA256。它改变负载模型，必须使用相同模型的批准基线。基础聊天基线默认不附加图片负载。

在 Docker Desktop 内的 Linux 构建容器里运行集成时，需要挂载 Docker socket 并设置 `TESTCONTAINERS_HOST_OVERRIDE=host.docker.internal`；GitHub 的原生 Linux runner 使用默认主机探测。该覆盖只指向本机测试容器宿主。

构建容器与依赖容器在同一 Docker bridge 时可加 `CPPCHAT_TEST_CONTAINER_NETWORK=bridge`，通过当前容器地址访问依赖，减少 Docker Desktop 端口转发的影响；它不会访问宿主数据库。地址会在容器启动后重新读取。覆盖率运行前清空当轮构建的 `.gcda` 计数，不累加历史验证结果。

GitHub 方法参考：[GoogleTest](https://github.com/google/googletest)、[Qt Test](https://github.com/qt/qtbase/blob/dev/src/testlib/doc/src/qttestlib-manual.qdoc)、[pytest](https://github.com/pytest-dev/pytest)、[Testcontainers](https://github.com/testcontainers/testcontainers-python)、[Hypothesis](https://github.com/HypothesisWorks/hypothesis)、[Toxiproxy](https://github.com/Shopify/toxiproxy)、[Mailpit](https://github.com/axllent/mailpit)、[k6](https://github.com/grafana/k6)。

覆盖率门槛统一使用源码普通条件分支，按 [gcovr 官方说明](https://gcovr.com/en/stable/faq.html#why-does-c-code-have-so-many-uncovered-branches) 排除编译器生成的异常展开和不可达分支；异常处理本身仍由数据库/网络故障及抛出任务用例验证。门槛仍是行 80%、分支 70%。原始 CFG 统计可以去掉 `--exclude-throw-branches --exclude-unreachable-branches` 单独导出，用于检查差异；不会将两种口径混用为同一基线。
