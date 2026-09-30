<div align="center">

# SearchAdvisor

**An in-combat turn advisor for *Slay the Spire* (1).**
A Java 8 ModTheSpire mod that reads the real fight and asks a native C++ search engine for this turn's line of play.

[简体中文](README.zh-CN.md) · [Architecture](DESIGN.md) · [Protocol](PROTOCOL.md) · [Validation](VALIDATION.md) · [License](#license)

![license](https://img.shields.io/badge/license-MIT-blue.svg)
![java](https://img.shields.io/badge/java-8-orange.svg)
![platform](https://img.shields.io/badge/platform-macOS%20arm64%20%7C%20x64-lightgrey.svg)
![status](https://img.shields.io/badge/status-experimental%20prototype-red.svg)
![parity](https://img.shields.io/badge/simulator%20parity-INCOMPLETE-critical.svg)

</div>

> [!WARNING]
> **Experimental prototype, simulator parity is INCOMPLETE.** Suggestions can disagree with real
> game rules. It was built and tested offline only — the game was never launched during development,
> so ModTheSpire loading, HUD layout, card-queue timing and auto-play have **not** been validated
> in-game. Auto-play is **OFF** by default. This is an assistant, not a bot that plays the run for you.

---

## What it does

During an Ironclad combat, SearchAdvisor captures the current state (hand, draw/discard/exhaust
piles, energy, powers, relics, potions, monsters and all six combat RNG streams), sends it to a
native search engine, and draws the recommended sequence for **this turn** on screen.

| Input | Action |
| --- | --- |
| `F8` / on-screen button | Run a search for the current turn |
| `F9` / on-screen button | Toggle auto-play (executes ordinary card plays) |
| `F10` / on-screen button | Cancel a running search |

The next card and its target are highlighted with a gold frame. Keys, simulation budget, timeout and
engine path are configurable. Auto-play executes one parsed card at a time, re-validates the game
state before every play, and **stops** on potions, card selection or end-turn so the player keeps
control.

## How it works

```text
 BaseMod main thread                     background thread            native process
 ───────────────────                     ─────────────────            ──────────────
 stable decision point
   └─ capture immutable snapshot ──────▶ EngineClient ── stdin ─────▶ sts-search-server
   └─ render text / gold highlights ◀── result queue ◀─ stdout ◀──── BattleScumSearcher2
   └─ queue ONE card, then wait                                                (C++17)
```

- **Process, not JNI.** The search is C++; the mod is Java. A crash or assert in the engine cannot
  take down the JVM, and a stuck search can be killed. The trade-off is per-request process startup.
- **One search per request.** A snapshot, a simulation budget and a timeout are sent; the engine runs
  `search()` **once** and returns the current-turn prefix. No reseeding, no retry, no cherry-picking.
- **Strict state guard.** Every action carries a `before` guard. The mod re-reads the real game state
  and aborts auto-play (and re-searches once) if hand, energy, target, powers, relics or RNG moved.

Architecture, field mapping and known gaps are documented in [DESIGN.md](DESIGN.md).
The wire format is a line-delimited JSON protocol, documented in [PROTOCOL.md](PROTOCOL.md).

## Requirements

- *Slay the Spire* (desktop build `12-18-2022`), Java 8.
- [ModTheSpire](https://github.com/kiooeht/ModTheSpire) `3.30.0` and
  [BaseMod](https://github.com/kiooeht/BaseMod) `5.56.0`.
- A matching `sts-search-server` executable for your OS/arch (macOS `arm64` and `x64` are built).

## Install (players)

1. Subscribe to **ModTheSpire** and **BaseMod** in the Steam Workshop.
2. Put `SearchAdvisor.jar` in the `mods/` folder, with a sibling `searchadvisor-engine/` directory:

   ```text
   mods/
     SearchAdvisor.jar
     SearchAdvisor.properties        # optional
     searchadvisor-engine/
       macos-arm64/sts-search-server
       macos-x64/sts-search-server
   ```

3. The mod locates the engine next to the loaded jar. If it is missing, the HUD shows
   *"engine not installed"* and the game keeps running. Nothing is hard-coded to a Steam path.

Copy [`SearchAdvisor.properties.example`](SearchAdvisor.properties.example) next to the jar to change
keys or budget. A Steam Workshop release is **not** published yet — see [WORKSHOP.md](WORKSHOP.md)
for the open questions (notably: shipping native executables).

## Build from source

### 1. Native search engine

```sh
cmake -S engine -B build/engine -DCMAKE_BUILD_TYPE=Release
cmake --build build/engine -j 4
```

The build reuses the read-only reference sources (see [THIRD_PARTY.md](THIRD_PARTY.md)) and emits
`build/engine/sts-search-server`. No Python/pybind runtime is required. macOS `x64`:

```sh
cmake -S engine -B build/engine-macos-x64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64
cmake --build build/engine-macos-x64 -j 4
```

Windows/Linux builds use the same first command on the target toolchain; no verified artifacts are
shipped for them yet. Do not rename a macOS binary to another platform.

### 2. The mod (Java 8)

`mod/local.properties` points at the three installed jars (`desktop-1.0.jar`, `ModTheSpire.jar`,
`BaseMod.jar`). They are read-only inputs and are never copied, installed or shaded.

```sh
# Standard Maven build (needs Maven + network):
python3 scripts/build_mod.py

# Offline javac build (Java 8 bytecode, verified locally):
export JAVA_HOME=/path/to/jdk
python3 scripts/build_mod.py --javac
```

Output: `mod/target/SearchAdvisor.jar` (contains `ModTheSpire.json` at its root).

### 3. Package for local install (does not publish)

```sh
python3 scripts/package.py \
  --native macos-arm64=build/engine/sts-search-server \
  --native macos-x64=build/engine-macos-x64/sts-search-server
```

## Test

Everything below runs **without launching the game**.

```sh
ctest --test-dir build/engine --output-on-failure   # engine protocol + fixture entry
python3 scripts/test_java.py                         # JVM protocol / guard / real IPC
python3 scripts/test_server.py                       # protocol unit tests
python3 scripts/search_fixture.py --budget 2000      # snapshot -> suggestions demo
```

`search_fixture.py` keeps stdin open until the result arrives — EOF is treated as a cancel, so a
`ping`-style one-shot pipe is **not** a valid search call. See [VALIDATION.md](VALIDATION.md) for the
full pass/fail record, including tests that initially failed.

## Configuration

`SearchAdvisor.properties` (next to the jar), or `-Dsearchadvisor.config=…` /
`-Dsearchadvisor.engine=…`:

| Key | Default | Meaning |
| --- | --- | --- |
| `searchKey` / `autoKey` / `stopKey` | `F8` / `F9` / `F10` | libGDX key names |
| `simulations` | `2000` | simulations for the single `search()` call (1–100000) |
| `timeoutMs` | `5000` | cooperative deadline (1–30000) |
| `enginePath` | auto | explicit path to `sts-search-server` |

Auto-play always starts OFF and resets when entering a new combat.

## Project layout

```text
engine/     C++17 search server (CMake); adapter, protocol, vendored search sources
mod/        Java 8 ModTheSpire mod (Maven): snapshot capture, guard, engine client, HUD
scripts/    build / package / test / fixture helpers
tests/      offline protocol + fixture tests (no game)
*.md        design, protocol, validation, workshop, third-party notes
```

## Limitations

- **Parity is INCOMPLETE.** Dynamic summons/splits, arbitrary per-card modifiers, unmapped powers and
  many private fields are rejected rather than guessed. Suggestions may be wrong or incomplete.
- Auto-play stops at potions, card-selection screens and end-turn; the player performs those.
- An already-queued card cannot be rolled back when auto-play stops.
- Windows `x64` and Linux `x64` binaries, code-signing and macOS compatibility range are unverified.
- No in-game acceptance has happened: loading, rendering, hotkey conflicts and queue timing are open.

## Roadmap

- In-game acceptance pass (loading, HUD at several resolutions, queue timing, interrupt handling).
- Extend snapshot coverage (dynamic monster slots, more relics/powers).
- Windows/Linux engine builds; consider JNI if process startup proves to be the bottleneck.

## Contributing

Issues and PRs are welcome. Please keep the honesty contract: do not present legal-action checks as
rule-parity proof, and always report failed tests. Run the offline test suite before a PR.

## License

MIT — see [LICENSE](LICENSE). Portions derive from
[gamerpuppy/sts_lightspeed](https://github.com/gamerpuppy/sts_lightspeed) and the `sts-rl-agent`
reference repository; [nlohmann/json](https://github.com/nlohmann/json) is vendored (MIT).
See [THIRD_PARTY.md](THIRD_PARTY.md). **No game jar, assets or decompiled source are included** —
you need your own copy of *Slay the Spire*.

<div align="center"><sub>SearchAdvisor is an unofficial fan project, not affiliated with Mega Crit.</sub></div>
