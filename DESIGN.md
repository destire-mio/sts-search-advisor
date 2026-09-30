# SearchAdvisor 第一阶段设计

## 目标与验收边界

SearchAdvisor 在铁甲战士的玩家回合读取真实状态，用已有 `BattleScumSearcher2` 搜索，在画面上列出本回合动作，并以金色框标记下一张牌和目标。F8／按钮触发搜索，F9／按钮切换自动出牌，F10／按钮停止。快捷键可配置，自动出牌在启动、换战斗和状态偏差时关闭。

交付是可编译、可离线运行的原型。禁止启动游戏的约束意味着界面布局、ModTheSpire 加载、游戏帧时序和自动出牌的实际体验没有验收。原版／模拟器一致性结论为 **INCOMPLETE**；搜索建议可能与原版规则有出入。UI 显示此限制，合法性测试不能替代规则一致性证明或最优性证明。

第一阶段支持普通玩家决策点。药水、选牌和结束回合列入建议，由玩家操作；自动模式执行普通出牌。遇到这些动作、状态不一致或任何错误，自动模式停止。跨回合自动战斗、全局路线、上传工坊、Windows/Linux 已验证产物不属于本次完成项。

## Java 与 C++ 的边界

| 方案 | 好处 | 成本／故障边界 | 本阶段判断 |
|---|---|---|---|
| JNI 原生库 | 避免序列化，可调用细粒度接口 | JVM ABI、线程附着、每平台动态库；C++ assert/崩溃会带走游戏；取消卡住的 native 调用困难 | 后续性能证据证明进程成本成瓶颈时评估 |
| 本地子进程 + JSON Lines | 复用 C++，进程故障与游戏隔离；协议可独立测试；可以终止卡住的引擎 | 启动、序列化和分发多个可执行文件的成本 | **选择此方案** |
| 纯 Java 移植 | 单一 jar，无原生分发 | 需要移植搜索、模拟器规则与 RNG；扩大一致性验证面，现有 C++ 修复需要重复维护 | 本阶段成本超过收益 |

```
BaseMod 主线程：稳定点捕获 → 不可变 JSON → 后台 EngineClient
                                               ↓ stdin
                              sts-search-server / C++ 独立进程
                                               ↓ stdout
BaseMod 主线程：请求编号 + 快照校验 ← 结果队列 ← 后台读取
                ↓
          文字 / 金色框 / 一次入队一张牌
```

Java 后台线程接触 JSON 和进程，不接触 AbstractDungeon、卡牌或怪物对象。所有游戏读写放在 `receivePostUpdate()`。`receiveRender()` 画文本与边框，不改变卡牌的发光属性、牌堆或 RNG。

服务器支持一个进程内多次请求、一个活动搜索和独立取消命令。Mod 的第一阶段客户端为每个搜索请求创建一个进程，收到结果后销毁。这让取消、进程退出和旧状态隔离容易核对，代价是每次的启动成本；服务器协议没有因此限制为一次性进程。错误不会触发重试。

## 搜索策略与预算

复用 `combat_engine/combat4r/engine-source/src/sim/search/BattleScumSearcher2.cpp`，并链接同一树中的模拟器、Action、ScumSearchAgent2 等源码。原项目 CMake 同时构建 Python 模块，默认输出还指向原仓库；本项目 CMake 复用它的源码集合、头文件和 `COMBAT3_TARGET_POLICY` 定义，独立建立静态库和可执行文件，输出放在本项目 build 下，不 `add_subdirectory` 原项目，不依赖 Python/pybind11 运行时。

每次请求构造一棵树，调用 **一次** `search(simulations)`。初始搜索 RNG 沿用源实现 `seed + floor`，战斗的六路 RNG 按真实状态导入；没有换种子、确定化采样、重试或多候选挑结果。返回 `bestActionSequence` 的当前回合前缀，遇结束回合、战斗终结或回合切换停止截取，逐步调用 `Action::isValidAction()` 后模拟执行。即使结果是一条预测失败的路径，也不另搜一条冒充成功。

`ScumSearchAgent2::playoutBattle()` 和 `combat_search_reuse.h::playoutReusing()` 会在一整场战斗中多轮规划；本阶段不调用这些完整战斗驱动。子树复用需要按原版执行后的状态、卡牌身份和 RNG 验证再重定位；当前导出完整性和跨回合证明不足，因此没有接入。此原型不是 P300 全战斗策略的等价替代，不继承历史胜率。

默认 2,000 次模拟／请求，接受 1–100,000 次。Boss 不额外乘预算。默认时限 5,000 ms，协议上限 30,000 ms。时限到达返回 TIMEOUT，取消返回 CANCELLED；两者都不返回部分计划，不根据耗时挑选结果。搜索副本在模拟间、树下降和随机 rollout 动作间检查取消／时限，游戏主线程不等待搜索或管道。单次模拟器动作内部不可被协作检查打断，所以 Java 另外设进程级超时；停止后 250 ms 尚未退出则强制终止，超时后另留 1 s 传输余量。停止不能撤销已交给游戏队列的一张牌。

搜索可复现的范围是相同快照、预算、引擎源码及工具链。源实现使用 `std::default_random_engine` 和标准分布；不同 C++ 标准库之间不承诺逐动作相同。跨平台应分别保存基线。

## 状态导出与映射

`Snapshot.java` 直接遍历游戏对象，生成与既有 CommunicationMod `game` 部分兼容的 JSON。它复用／适配：

- `CombatStatePatch` → `CombatExtras`：六路 RNG、回合内计数、回合末洗牌模式、朝向、遗物私有开关、怪物当前／历史招式及私有字段。去掉 CommunicationMod 依赖、patch 注解和命令执行补丁。
- `FullRngState`：完整 RNG 诊断记录，保持只读；不初始化、不复制、不采样、不重置游戏 RNG。完整运行 RNG 未进入 C++ 战斗模型的部分不能作为一致性已证明的依据。
- `GameRngPatch` 不加载：它挂到 CommunicationMod；改由搜索触发点调用诊断导出。

本机 API 通过安装 jar 的 `javap` 签名确认，包括 BaseMod 订阅接口、CardQueueItem 三参数构造、GameActionManager.addCardQueueItem、AbstractCard.canUse、怪物 move 和能力私有字段。项目没有加入反编译游戏源码。

| 原版来源／字段 | 模拟器目标 | 约束与缺口 |
|---|---|---|
| Settings.seed、楼层、升阶、GameActionManager.turn | seed、floorNum、ascension、turn-1 | Java long 用十进制字符串保存位模式；升阶 0–20 |
| 玩家 HP / maxHP / block / EnergyPanel.totalCount / gold | Player 数值 | 保留 isBloodied 历史标记，不能只从半血阈值推断 |
| energy.energy、gameHandSize、Berserk | energyPerTurn、cardDrawPerTurn | Berserk 加入每回合能量，不重复作为状态导入 |
| 手牌／抽牌／弃牌／消耗堆的 group 顺序 | CardManager 的四个容器 | 顺序不排序、不洗牌；原版 UUID → 本次导入 uniqueId；新生成牌暂按索引及属性匹配 |
| cardID、timesUpgraded、costForTurn、cost、freeToPlayOnce、misc | CardInstance | Strike_R/Defend_R 等别名转换；Rampage/Ritual Dagger/Searing Blow 有专项字段处理；手牌 >10、未知 ID、重复 UUID 拒绝 |
| 每实例 exhaust / ethereal 改写 | 引擎按卡牌定义推导 | 无对应持久字段时拒绝已知覆盖，不静默丢弃；其他卡牌修改器仍有覆盖缺口 |
| 玩家 powers（含 justApplied、Combust hpLoss、Panache damage、Bomb damage/countdown） | PlayerStatus、powerOrder、bombs | 未识别能力拒绝；非 Ironclad、第三方扩展机制没有通用适配 |
| 遗物 ID、counter、Centennial Puzzle / Red Skull / Necronomicon / Orange Pellets 私有状态 | relic bits、计数器、历史标记 | 必需开关缺失则拒绝；不是所有遗物／第三方字段的完整序列化 |
| 药水栏（包含空位） | potionCapacity / potions | 搜索可以建议用药或丢药；自动执行停在药水步骤 |
| 怪物 Java ID、HP/block、halfDead/isGone、move、moveHistory | MonsterId、状态、moveHistory | 静态编队保留死亡槽位；目标索引是原版列表索引 |
| 怪物本地 byte move ID | MMID | 使用 steam_mcts.py 已有表；未知招式拒绝；隐藏意图仍使用内部招式 |
| Louse biteDamage、Book stabCount、Guardian dmgThreshold/isOpen 等 | miscInfo / uniquePower / status fields | 字段来源按既有桥接；出现未暴露状态不能宣称完全一致 |
| ai/card_random/misc/monster_hp/potion/shuffle 的 seed0/seed1/counter | 六个原生 Random | 六路必须齐全，不造 seed；uint64 字符串避免精度损失 |
| end_turn_shuffle mode / shared_seed48 | EndTurnShuffleState | BaseMod seeded 与 Java shared 分开；未初始化的 java_shared RNG 拒绝 |
| actions/cardQueue/monsterQueue、选择界面、拖牌状态 | 稳定输入边界 | 只导出空队列、WAITING_ON_USER；不尝试导入半执行中的 action 闭包 |

当前拒绝的动态怪物槽位：Reptomancer/Dagger、Gremlin Leader、Bronze Automaton/Orb、Collector/Torch Head、Slime Boss、Acid/Spike Slime L。既有 `canonical_monsters()` 含这些场景的槽位变换，但复制槽位算法不足以证明 Java 实例身份与预测后续召唤一致；需要专项适配测试后扩展。拒绝信息走 UI，不应让游戏崩溃。

源 `sim_patch/parity/RESULTS-20260929-AUDIT-CLOSE.md` 写明全局 parity 是 **INCOMPLETE**。没有证明所有动态分支、无界行动序列、无 Mod 原版、全部私有字段、广泛自然新种子和底层 UI 帧。固定预算建议合法，不代表最优，也不保证能按预测结果执行。参考桥接的字段有覆盖边界，本次移植没有消除这些边界。

## 自动出牌安全与生命周期

1. 主线程保存当前 room 和 player 对象身份、请求 token、完整输入 JSON；离开战斗或换实例使 token 作废并取消进程。请求编号和 token 都必须匹配；迟到结果丢弃。
2. 接收结果时用新快照对比发送时状态，排除帧时长和完整运行 RNG 诊断中的非战斗流，其余字段须一致。
3. 每张牌入队前重读状态，对比预测前置条件：四个牌堆顺序、UUID（可用时）、费用／升级／免费状态、能量、HP/block、回合内计数、玩家能力、药水、支持的遗物计数和开关、怪物列表／HP/block／招式／能力、六路 RNG。检查游戏 `card.canUse(player,target)`，目标必须存活且可攻击。
4. 只将一张牌放入游戏队列，不直接调用 card.use；能量使用当前 EnergyPanel 数值。等待游戏结算和下一稳定点，再检查下一步。Surrounded 的朝向更新沿用旧桥接做法。
5. 前置状态不一致时关闭自动模式、丢弃计划并对新状态发起一次搜索。相同输入自动重搜被抑制，避免模拟差异造成无限循环。错误、超时不重试。手动 F8 是新的明确请求。
6. F10 即使在选牌界面或动画阶段也处理；停止按钮同样可用。已入队牌由游戏完成，原型不清空玩家／其他 Mod 的动作队列。自动模式遇药水、选择或 end_turn 停止，交给玩家。

这里的 guard 是有限字段检查，不是完整原版状态同构证明；第三方 Mod 修改隐藏状态、特殊生成牌身份以及尚未建模的规则仍有风险。第一阶段默认关掉自动模式，并在 UI 保留实验性质提示。

## 分发与引擎定位

ModTheSpire 的元数据 `ModTheSpire.json` 位于 jar 根目录，依赖 `basemod`。官方加载约定是将 mod jar 放入 mods 目录，由 ModTheSpire 选择加载。[ModTheSpire 官方说明](https://github.com/kiooeht/ModTheSpire)；本地 BaseMod 5.56.0 jar 的同名元数据也用于核对。工坊上传要求由 `WORKSHOP.md` 接续调研，本文件不代替上传验收。

```
SearchAdvisor.jar                 # 内含 ModTheSpire.json
SearchAdvisor.properties          # 可选，玩家从 example 复制
searchadvisor-engine/
  macos-arm64/sts-search-server
  macos-x64/sts-search-server
  windows-x64/sts-search-server.exe
  linux-x64/sts-search-server
```

默认通过 Loader.MODINFOS 中 searchadvisor 的 jarURL 定位相邻目录，而不是假定 Steam 或工作目录路径；CodeSource 作为开发运行备选。按 JVM 的 os.name / os.arch 选择平台，Mac 上 Rosetta 的 x64 JVM 选择 macos-x64。显式 `-Dsearchadvisor.engine=/path` 或配置 enginePath 可覆盖，ProcessBuilder 参数列表处理空格，不经 shell。

找不到文件时 UI 提示“未安装搜索引擎”，自动模式关闭；权限、架构、进程崩溃、协议版本错误均作为搜索错误展示。异常被 Mod 层捕获，保持游戏循环。Linux/Windows/macOS 分别在对应工具链构建，不把 macOS 文件改名伪装其他平台。macOS 可以通过 Apple SDK 交叉构建两个架构；本次提供 arm64 和 x64，Windows/Linux 待对应环境编译测试。发布前还需要平台签名／运行权限、运行库和分发许可核对；这里没有执行发布或更改系统安全设置。

## 构建与测试策略

三份安装 jar 使用 Maven systemPath 属性引用：sts.jar、mts.jar、basemod.jar。`scripts/build_mod.py` 从本地 local.properties 读取并转为 Maven 参数；没有 install-file、shade 或拷贝依赖操作。离线 javac 构建目标为 Java 8；Gson 复用游戏环境。原生构建和夹具读取都把参考仓库当只读输入。

`tests/test_server.py` 使用四份 parity 原版记录的固定前 3 个完整快照，共 12 个，不按搜索结果筛选。首步对照夹具里的原版 legal_actions；整段从原始输入重新导入，按顺序用模拟器验证，期间不重置状态。早期 alignment gzip 记录缺少共享 RNG，因此测试拒绝行为而非补造字段。该区别记录在验证报告。

JVM 测试覆盖协议、原版根状态 guard、手牌／能量／目标／能力／RNG 变动拒绝、迟到 token、缺引擎、真实原生进程交互，以及不响应取消的假进程终止。测试启动普通 Java 测试入口，不启动游戏、ModTheSpire 或窗口。真实游戏加载、渲染与队列时机列入后续手动验收，不能用这些离线检查宣称通过。
