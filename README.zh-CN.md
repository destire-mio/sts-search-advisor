<div align="center">

# SearchAdvisor

**《杀戮尖塔 1》局内出牌顾问。**
一个 Java 8 的 ModTheSpire 模组：读取真实战斗状态，交给原生 C++ 搜索服务，给出本回合的出牌建议。

[English](README.md) · [架构](DESIGN.md) · [协议](PROTOCOL.md) · [验证](VALIDATION.md) · [许可](#许可)

![license](https://img.shields.io/badge/license-MIT-blue.svg)
![java](https://img.shields.io/badge/java-8-orange.svg)
![platform](https://img.shields.io/badge/platform-macOS%20arm64%20%7C%20x64-lightgrey.svg)
![status](https://img.shields.io/badge/status-%E5%AE%9E%E9%AA%8C%E5%8E%9F%E5%9E%8B-red.svg)
![parity](https://img.shields.io/badge/%E6%A8%A1%E6%8B%9F%E5%99%A8%E4%B8%80%E8%87%B4%E6%80%A7-INCOMPLETE-critical.svg)

</div>

> [!WARNING]
> **实验原型，模拟器一致性为 INCOMPLETE。** 建议可能和原版规则有出入。开发过程全程离线，
> 从未启动游戏，因此 ModTheSpire 加载、界面布局、出牌队列时序和自动出牌**都没有经过游戏内验收**。
> 自动模式**默认关闭**。它是助手，不是替你打完整局的 bot。

---

## 它能做什么

铁甲战士战斗中，SearchAdvisor 抓取当前状态（手牌、抽牌／弃牌／消耗堆、能量、能力、遗物、
药水、怪物，以及全部六路战斗 RNG），发给原生搜索服务，把**本回合**的出牌序列画在屏幕上。

| 操作 | 行为 |
| --- | --- |
| `F8`／屏幕按钮 | 对当前回合执行一次搜索 |
| `F9`／屏幕按钮 | 切换自动出牌（执行普通出牌） |
| `F10`／屏幕按钮 | 取消正在进行的搜索 |

下一张牌和目标用金色框高亮。快捷键、模拟次数、时限和引擎路径均可配置。自动模式逐张出牌，
每张入队前重新校验游戏状态，遇到药水、选牌或结束回合就**停下**，把控制权交还玩家。

## 工作原理

```text
 BaseMod 主线程                          后台线程                      原生进程
 ──────────────────                      ──────────                    ────────
 稳定决策点
   └─ 抓取不可变快照 ──────────────────▶ EngineClient ── stdin ─────▶ sts-search-server
   └─ 绘制文字／金色高亮 ◀──────────── 结果队列 ◀─ stdout ◀──────── BattleScumSearcher2
   └─ 只入队一张牌，然后等待                                               (C++17)
```

- **子进程，不是 JNI。** 搜索是 C++，Mod 是 Java。引擎崩溃或断言不会带走 JVM，卡死的搜索可以
  杀掉。代价是每次请求的进程启动开销。
- **每次请求只搜一次。** 发送快照、模拟预算和时限；引擎调用**一次** `search()`，返回当前回合前缀。
  不换种子、不重试、不挑结果。
- **严格状态守卫。** 每个动作都带 `before` guard。Mod 重新读取真实状态，手牌、能量、目标、
  能力、遗物或 RNG 有变动时立刻关闭自动模式并重新搜索一次。

架构、字段映射与已知缺口见 [DESIGN.md](DESIGN.md)；线协议（JSON Lines）见 [PROTOCOL.md](PROTOCOL.md)。

## 依赖

- 《杀戮尖塔 1》（桌面版 `12-18-2022`），Java 8。
- [ModTheSpire](https://github.com/kiooeht/ModTheSpire) `3.30.0` 与
  [BaseMod](https://github.com/kiooeht/BaseMod) `5.56.0`。
- 与系统／架构匹配的 `sts-search-server` 可执行文件（当前构建了 macOS `arm64` 与 `x64`）。

## 安装（玩家）

1. 在 Steam 创意工坊订阅 **ModTheSpire** 和 **BaseMod**。
2. 把 `SearchAdvisor.jar` 放进 `mods/`，与 `searchadvisor-engine/` 目录相邻：

   ```text
   mods/
     SearchAdvisor.jar
     SearchAdvisor.properties        # 可选
     searchadvisor-engine/
       macos-arm64/sts-search-server
       macos-x64/sts-search-server
   ```

3. Mod 从已加载的 jar 定位相邻引擎。找不到时界面显示「未安装搜索引擎」，游戏继续运行，
   不硬编码 Steam 路径。

把 [`SearchAdvisor.properties.example`](SearchAdvisor.properties.example) 复制到 jar 旁边即可改键位或预算。
创意工坊发布**尚未上线**，待定问题（尤其是原生可执行文件的合规性）见 [WORKSHOP.md](WORKSHOP.md)。

## 从源码构建

### 1. 原生搜索服务

```sh
cmake -S engine -B build/engine -DCMAKE_BUILD_TYPE=Release
cmake --build build/engine -j 4
```

构建复用只读参考源码（见 [THIRD_PARTY.md](THIRD_PARTY.md)），产物为 `build/engine/sts-search-server`，
不依赖 Python／pybind 运行时。macOS `x64`：

```sh
cmake -S engine -B build/engine-macos-x64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64
cmake --build build/engine-macos-x64 -j 4
```

Windows／Linux 在对应工具链运行第一条命令；目前没有可验证的产物。不要把 macOS 二进制改名冒充其他平台。

### 2. Mod（Java 8）

`mod/local.properties` 指向本机安装的三份 jar（`desktop-1.0.jar`、`ModTheSpire.jar`、`BaseMod.jar`）。
它们是只读输入，不复制、不安装、不 shade。

```sh
# 标准 Maven 构建（需要 Maven 与网络）：
python3 scripts/build_mod.py

# 离线 javac 构建（Java 8 字节码，本机已验证）：
export JAVA_HOME=/path/to/jdk
python3 scripts/build_mod.py --javac
```

产物：`mod/target/SearchAdvisor.jar`（根目录含 `ModTheSpire.json`）。

### 3. 本地打包（不发布）

```sh
python3 scripts/package.py \
  --native macos-arm64=build/engine/sts-search-server \
  --native macos-x64=build/engine-macos-x64/sts-search-server
```

## 测试

以下全部**不启动游戏**：

```sh
ctest --test-dir build/engine --output-on-failure   # 引擎协议 + 夹具入口
python3 scripts/test_java.py                         # JVM 协议／guard／真实 IPC
python3 scripts/test_server.py                       # 协议单元测试
python3 scripts/search_fixture.py --budget 2000      # 原版快照 -> 建议演示
```

`search_fixture.py` 会保持输入开放直到收到结果——协议把 EOF 当作取消，所以像 `ping` 那样发完就关输入
**不是**合法的 search 调用。完整的通过与失败记录（含最初失败的测试）见 [VALIDATION.md](VALIDATION.md)。

## 配置

`SearchAdvisor.properties`（放在 jar 旁边），或 JVM 参数 `-Dsearchadvisor.config=…`／`-Dsearchadvisor.engine=…`：

| 键 | 默认 | 含义 |
| --- | --- | --- |
| `searchKey` / `autoKey` / `stopKey` | `F8` / `F9` / `F10` | libGDX 按键名称 |
| `simulations` | `2000` | 单次 `search()` 的模拟次数（1–100000） |
| `timeoutMs` | `5000` | 协作时限（1–30000） |
| `enginePath` | 自动 | 显式指定 `sts-search-server` 路径 |

自动模式始终以关闭启动，进入新战斗时重置。

## 目录结构

```text
engine/     C++17 搜索服务（CMake）：适配层、协议、vendored 搜索源码
mod/        Java 8 ModTheSpire 模组（Maven）：快照抓取、guard、引擎客户端、HUD
scripts/    构建／打包／测试／夹具脚本
tests/      离线协议与夹具测试（不启动游戏）
*.md        设计、协议、验证、工坊、第三方说明
```

## 已知限制

- **一致性为 INCOMPLETE。** 动态召唤／分裂、任意卡牌实例修改器、未映射能力和大量私有字段会被拒绝，
  而不是猜测；建议可能错误或不完整。
- 自动模式遇药水、选牌界面和结束回合会停止，由玩家操作。
- 自动模式停止时，已入队的一张牌无法回滚。
- Windows `x64`／Linux `x64` 产物、代码签名和 macOS 兼容范围未验证。
- 尚未游戏内验收：加载、渲染、热键冲突和队列时序都待验证。

## 路线图

- 游戏内验收（加载、多分辨率 HUD、队列时序、打断处理）。
- 扩展快照覆盖（动态怪物槽位、更多遗物／能力）。
- Windows／Linux 引擎构建；若进程启动成为瓶颈，再评估 JNI。

## 贡献

欢迎 issue 与 PR。请遵守诚实约定：不要把合法动作检查当成规则一致性证明，失败测试要如实汇报。
提 PR 前先跑离线测试套件。

## 许可

MIT，见 [LICENSE](LICENSE)。部分代码来自
[gamerpuppy/sts_lightspeed](https://github.com/gamerpuppy/sts_lightspeed) 与 `sts-rl-agent` 参考仓库；
内嵌 [nlohmann/json](https://github.com/nlohmann/json)（MIT）。详见 [THIRD_PARTY.md](THIRD_PARTY.md)。
**不包含任何游戏 jar、资源或反编译源码**——你需要自备《杀戮尖塔》。

<div align="center"><sub>SearchAdvisor 是非官方同人项目，与 Mega Crit 无关。</sub></div>
