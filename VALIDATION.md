# 第一阶段验证记录

日期：2026-09-30。参考仓库 HEAD：`3f6467ff0b38e152ef147556e32cc0127b478ed2`。参考仓库作为只读输入；开始和收尾的 `git status --short` 都只有原有 `?? scratchpad/`。源码 SHA 见 `engine/PROVENANCE.json` 与 `build/evidence/source-manifest.json`。

## 完成项

| 检查 | 结果 | 证据 |
|---|---|---|
| CMake / C++17 macOS arm64 构建 | 通过，独立可执行文件 | `build/evidence/cmake-build.log` |
| macOS x64 交叉构建、Rosetta 下协议套件 | 通过 | `build/evidence/cmake-x64-build.log`、`protocol-tests-x64.log` |
| Java 8 目标编译 | 通过，所有 class major version 为 52 | `build/evidence/javac-build.log`；`mod/target/SearchAdvisor.jar` |
| CTest 协议与夹具入口 | 1/1 通过；内部 13 个 unittest | `build/evidence/ctest.log`、`protocol-tests.log` |
| 原版正向快照 | 12 个固定捕获，来自 4 个 parity 文件；首步在原版 legal_actions 中，完整当前回合前缀通过模拟器逐动作验证 | `build/evidence/fixture-results.json`、`guard-vectors.json` |
| 旧 alignment gzip 快照 | 缺少 end_turn_shuffle，按合同拒绝；这是负向检查，不能算导入成功 | `tests/test_server.py::test_old_alignment_capture_rejected` |
| JVM 协议与 guard | 210 个断言通过，包含实际原生 IPC、状态变更、错误 token、取消、挂起进程终止和缺引擎 | `build/evidence/java-tests.log` |
| Java 8 运行时 | 使用本机游戏附带的 OpenJDK 1.8.0_252 运行测试入口通过；没有启动游戏类或窗口 | `build/evidence/java8-tests.log` |
| 本地演示 | 原版快照 → 2,000 次模拟 → Carnage、End turn 建议 | `build/evidence/demo-response.json` |
| 产物检查 | jar 根目录包含 ModTheSpire.json，class 均在 searchadvisor 包；没有依赖 jar／游戏 class | `build/evidence/artifact-check.json` |

测试使用 `tests/fixtures.py` 固定选择四份文件的前 3 个完整捕获，选择规则不读取搜索结果。没有给旧记录伪造状态、换种子或按成功与否筛选快照。对整条建议的回放使用模拟器，不是原版 Java 游戏的实际执行；原版证据只覆盖根状态的合法动作集合。三种证明不能混同：编译通过、离线动作合法、游戏内行为验收。

## 构建与测试期间的失败

- 首次 Java 编译使用了本机 libGDX 不存在的 `Color.CREAM`，编译失败；改为 `Color.WHITE` 后编译通过。
- JVM 状态变更测试最初假定所有夹具都有手牌，在空手牌记录上抛出 IndexOutOfBoundsException；修改测试，选择现有牌堆中的卡牌后通过。没有把此错误归因于搜索器。
- 扩展药水 guard 后，原版 `Potion Slot` 与模拟器 `EmptyPotionId` 命名不同，根状态 guard 测试失败；补充对应映射后通过。
- `curl` 下载 Maven 报 `Could not resolve host: repo.maven.apache.org`。本机没有 Maven，因此 **Maven package 没有执行成功记录**。使用本机 JDK 的离线路径完成编译及 Java 8 运行时检查，不将它记为 Maven 验收。

编译仍有参考 C++ 头文件 unused-function 警告、JDK 26 关于 Java 8 目标的弃用提示，以及继承导出器的 unchecked 泛型提示；构建没有把这些提示算成失败，也没有修改参考仓库来消除它们。

## 性能观察的范围

在当前 macOS arm64 主机，对索引 0、6、9 的三个固定快照各执行一次 2,000 次模拟，进程管道阶段的观测耗时为 14.54、3.42、10.49 ms，均在 5,000 ms 预算内。这些夹具包含空手牌／低血量敌人等简单状态，计时从进程创建后发送请求开始，**不是**全启动耗时，也不代表复杂战斗、Boss 或其他机器的延迟。原始条件和记录见 `build/evidence/budget-probes.json`。不据此宣称实时性能达标。

## 缺口和后续验收

游戏进程没有启动。ModTheSpire 加载、按钮／高亮的画面、真实出牌队列、热键在游戏界面中的冲突、不同分辨率及与其他 Mod 的交互没有验证。Windows x64 / Linux x64 没有本次构建或运行记录。两个 macOS 产物没有经过发布签名或低版本 macOS 兼容性验收。

动态召唤／分裂编队、任意卡牌实例修改器、未映射能力和全部私有状态没有获得完整支持。自动执行遇药水、选牌、结束回合会停止。手动变更、模拟偏差或 guard 不能匹配时关闭自动模式并重新搜索；无法回滚已入队的一张牌。

parity 结论为 **INCOMPLETE**。局部合法性、12 个正向快照和一次预算内搜索不能证明建议最优、跨种子有效或完整原版一致性。没有 commit、push、上传工坊、修改参考仓库、改写 Steam 目录或 ~/Applications。
