# SearchAdvisor JSON Lines 协议 v1

## 传输和生命周期

启动 `sts-search-server`，UTF-8 stdin/stdout 每行一个 JSON 对象，换行结束记录。没有网络监听、shell 或外部下载。stdout 只允许协议响应，模拟器日志重定向 stderr；调用方必须读取 stderr 防止管道写满。请求／响应上限 4 MiB。EOF 表示取消活动计算并关闭进程，调用方应在收到搜索响应后关闭 stdin。

所有请求必须有整数 `version: 1`、1–128 字符的字符串 `id`、字符串 `op`。一个进程内 ID 不复用，最多 10,000 个；之后建立新进程。一个活动计算占据一个 worker，其他 search/inspect/validate 返回 BUSY；ping 和 cancel 在输入线程处理。取消应使用独立 ID。不要用 wall clock 或响应速度来决定重试挑选计划。

## 请求

探测：

```json
{"version":1,"id":"hello-1","op":"ping"}
```

搜索请求的结构：

| 字段 | 类型／范围 | 含义 |
|---|---|---|
| version / id / op | `1` / string / `"search"` | 协议版本与关联 ID |
| snapshot_token | string，最多 128 字符 | 客户端状态版本，原样回传；客户端仍需验证实际状态 |
| simulations | integer，1–100000 | 唯一一次 search() 的预算 |
| timeout_ms | integer，1–30000；默认 5000 | 从请求分派开始的协作时限 |
| snapshot | object | 下述原版 game 快照 |

生成一份来自原版夹具的可执行请求并读取响应：

```sh
python3 scripts/search_fixture.py --budget 2000
```

`inspect` 使用相同的 snapshot / snapshot_token / timeout_ms，省略 simulations；执行快照导入并返回 imported_guard，不搜索。`validate` 在此基础上添加 `actions: [uint32, ...]`，最多 512 个；从原始输入重建一份状态，验证并顺序执行动作，遇到非法动作、结束回合后的动作或跨回合动作返回错误。它用于离线校验，不提供绕过原版 canUse 的执行权限。

取消：

```json
{"version":1,"id":"cancel-2","op":"cancel","request_id":"search-1"}
```

取消确认：

```json
{"version":1,"id":"cancel-2","ok":true,"accepted":true,"request_id":"search-1"}
```

`accepted` 只在此刻活动请求 ID 匹配时为 true。确认与被取消请求的 CANCELLED 响应各占一行，按各自 id 关联，不能假定两个响应的先后顺序。匹配失败不影响其他请求；成功响应已发出后再取消会返回 false。CANCELLED / TIMEOUT 不携带部分动作。

## 快照模式

快照使用既有原版导出的 `view.game` 形状，不是整个 view，也不是 Python pybind 对象。当前只接受 `class: "IRONCLAD"`、`screen_type: "NONE"`、稳定且有存活敌人的玩家决策点。

必需的顶层字段：`class, screen_type, seed, floor, ascension_level, gold, relics, potions, combat_state`。seed 推荐无符号十进制字符串；服务兼容原版历史记录的有符号整数位模式。

combat_state 必需字段：

- `turn`（从 1 开始），`player` 的 `current_hp,max_hp,block,energy,powers`；`is_bloodied`、`energy_per_turn`、`card_draw_per_turn`。
- `cards_played_this_turn,attacks_played_this_turn,skills_played_this_turn,cards_discarded_this_turn,times_damaged`。
- `hand,draw_pile,discard_pile,exhaust_pile`：按原版容器顺序，每张含 `id,uuid,upgrades,cost,base_cost,free_to_play_once`，以及相关 `misc,base_damage,exhausts,ethereal`。总数上限 500，手牌上限 10；原型不允许丢弃 UUID 或重复 UUID。
- `monsters`：保留原版列表槽位，每项 `id,current_hp,max_hp,block,is_gone,half_dead,powers,internal,move_id`，可含 `last_move_id,move_base_damage`。需要的私有字段取决于敌人，例如 Louse 的 `internal.biteDamage`。未知招式与动态槽位编队拒绝。
- `rngs` 必须有 `ai,card_random,misc,monster_hp,potion,shuffle`。每项 `seed0,seed1,counter`；种子字使用无符号十进制字符串，counter 非负。缺少 RNG 不执行补种子。
- `end_turn_shuffle` 包含 `mode: "basemod_seeded" | "java_shared"`、布尔 `shared_rng_initialized`；true 时需要 `[0,2^48)` 整数 `shared_seed48`；false 时不得提供 seed。java_shared 且未初始化会引入未知随机源，服务拒绝。
- `relic_combat_state` 是对象。拥有相关遗物时必须包含 `centennial_puzzle_used,red_skull_active,necronomicon_used,orange_pellets_mask` 中对应字段。`facing_monster_index` 在 Surrounded 时必需。

powers 的 id 是原版 ID，amount 是原版层数；just_applied、Combust 的 misc、Panache/TheBomb 的 damage、Stasis 的 card 必须按机制提供。relics 项含 id/counter，potions 项含 id，空药水栏也保留。`frame_delta_seconds` 可省略，默认 1/60。完整原版捕获参见测试读入的夹具；`DESIGN.md` 列出映射与缺口。

`full_rng_state` 可作为诊断附带，但当前 C++ 模型不消费其中其余运行级流。它存在或 complete=true 不代表引擎已覆盖全部 RNG。未参与规则的显示字段可以存在，不用于计算。

## 搜索响应

成功字段：`version,id,ok:true,snapshot_token,parity:"INCOMPLETE",search_calls:1,simulations,complete_turn,stop_reason,actions,final_guard`。simulations 是实际完成的模拟数；取消／超时不算成功。`complete_turn` 表示前缀覆盖回合结束或战斗终结，不表示预测胜利。`stop_reason` 为 `turn_end_or_combat_end` 或 `plan_exhausted`。后者必须在 UI 视为不完整建议。

actions 中每项具有：

| 字段 | 含义 |
|---|---|
| bits | 供 validate 重放的 uint32 原生编码；不应依赖其布局实现游戏点击 |
| type | play / end_turn / potion / select |
| label | 人类可读文本 |
| before | 此动作执行前的预测 guard |
| hand_index / card | play 的零起始手牌索引和牌属性；不是固定贯穿整回合的初始索引 |
| target_index | play / potion 的零起始原版怪物槽位；-1 无目标 |
| potion_index / discard | potion 的栏位和是否丢弃；由玩家操作 |
| task / selected_indices | select 的模拟器任务及零起始选择索引；界面映射待完善，自动模式不执行 |

`card` 含 key（去标点的小写引擎枚举名）、cost、base_cost、upgrades、free，以及可用时的原版 uuid。生成牌在预测中没有原版 UUID，因此 guard 只对其位置和属性进行校验。`before` 还包含玩家资源、牌堆、回合计数、能力、药水、遗物计数／开关、怪物招式／能力和六路 RNG；Java 必须用新读取的原版状态检查这些字段，再调用原版 canUse。

end_turn 不在服务提取前缀时执行对手回合；此时 final_guard 是结束回合前状态。终局或普通出牌后的 final_guard 是已模拟的状态。它是模拟器预测，不是权威原版快照。

## 错误响应

```json
{"version":1,"id":"search-1","ok":false,"error":{"code":"CANCELLED","message":"no partial plan is returned"}}
```

| 错误码 | 原因／客户端行为 |
|---|---|
| INVALID_JSON | 解析失败；修正下一行，进程仍可用 |
| BAD_REQUEST | 外层类型、预算、时限或长度错误 |
| VERSION_MISMATCH | 协议版本错误；不要执行响应 |
| DUPLICATE_ID | 此会话 ID 已使用 |
| UNKNOWN_OP | 不支持的操作 |
| BUSY | 已有活动计算；不要并发启动同一状态的替代搜索 |
| INVALID_SNAPSHOT | 快照字段缺失或类型错误 |
| UNSUPPORTED_STATE | 未映射机制、容量／数值限制、非稳定状态，或 validate 的非法动作 |
| CANCELLED | 请求被取消，无计划 |
| TIMEOUT | 时限到，无计划，不重试挑结果 |
| SEARCH_FAILED | 模拟器异常或预算内没有可执行计划 |
| LIMIT | 会话请求量或响应大小超限 |

本机路径不存在属于 Java 客户端状态，显示“未安装搜索引擎”，不需要启动服务来报告。断管、原生崩溃和断言退出视为请求失败；客户端关闭自动模式并回收进程。协议有错误恢复，不保证恶意或任意损坏快照都能绕过底层引擎断言；进程隔离确保这类故障不会成为 JVM 内的 native 崩溃。
