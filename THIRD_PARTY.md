# 来源与许可

搜索器、模拟器、快照导入器与状态导出辅助代码来自只读参考仓库 `sts-rl-agent-principles`，其 MIT 许可全文保留在 `LICENSE`。该仓库声明的作者包括 sts-rl-agent contributors 和 gamerpuppy / sts_lightspeed。`engine/PROVENANCE.json` 记录复制或适配文件的原始 SHA-256；`build/evidence/source-manifest.json` 记录本次构建读取的引擎文件。

`engine/vendor/snapshot_import.h` 是 `bindings/slaythespire.cpp` 中快照重建部分的 JSON 适配；强制使用原版 RNG，删除确定化换种子兜底。`engine/vendor/BattleScumSearcher2.cpp` 是搜索源码副本，增加协作取消轮询，不改变搜索选择或 RNG。`name_tables.h` 与 `guard-aliases.json` 提取自 `steam/steam_mcts.py` 的名称和招式表。

`CombatExtras.java` 从 `CombatStatePatch.java` 提取战斗字段导出，删除 CommunicationMod patch 注解和命令执行 patch。`FullRngState.java` 更改包名，用于诊断快照。

nlohmann/json 头文件从参考仓库的 `combat_engine/third_party/nlohmann/json.hpp` 只读编译，原文件内包含 MIT 许可。打包目录的 `NLOHMANN-LICENSE.txt` 保留该声明。Gson、libGDX、游戏 API、ModTheSpire 和 BaseMod 来自玩家安装环境，项目产物没有复制或打包这些依赖 jar。项目文件没有游戏反编译源码。
