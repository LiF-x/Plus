---
title: Caravan event proposal
status: design
domain: design
tags: [ai-behavior-tree, caravan, road-following, pathfinding, custom-node]
related: [ai_and_spawning.md, hostile-npc-ai-path-comparison.md, mysql_access.md, sector_handoff_design.md]
sources: [source/server/cm_offsets.h, source/server/hooks/ai/ABI_NOTES.md, source/server/hooks/ai/hook_behavior_node.cpp, docs/ai_and_spawning.md, docs/hostile-npc-ai-path-comparison.md]
updated: 2026-06-26
---

# Caravan event proposal — road-following AI cart-horse

> **TL;DR.** A server-side event: an AI horse-cart travels A → B, **following player-built roads where any exist** (else a direct line), and **fights back when attacked**. The hard part — "follow roads" — is *not* solved inside the engine A*; it is solved offline by running our **own substance-weighted A\*** over the road tiles in MySQL `geo_patch` to emit a waypoint chain, then letting the stock engine pathfinder walk each short segment. Movement is driven by a LiFx custom behavior-tree node (`WalkWaypoints`) built on the now-merged-and-verified `LifxLogNode`/`TimeOfDayBetween`/`GoToPoint` node scaffolding (PRs #120/#122/#124). The Phase-0 blocker — what `getAiData()` returns and how to set a move-target — is **closed**: `getAiData()` (`0x1530D0`) is the per-node AI-context **blackboard** pointer, and `GoToPosition::process` (`0x2E5C70`) is the verified movement template. Phase 1 (single self-defending cart-horse) is plannable; the escort group is deferred to a later PR.

This page consolidates the design and the underlying RE so the caravan work can be implemented without re-deriving the AI ABI. Status is **design** for the caravan itself; the node-injection primitives it builds on are **runtime-verified** (see [Status & provenance](#status--provenance)).

---

## 1. Goal & scope

Phase 1 delivers a **single AI horse-cart** that travels A → B following roads where they exist and engages players if attacked. A multi-NPC escort group is a follow-up phase. The requirement is "follow roads *if there are any*" — a fresh world has none, so the absence of a road path must degrade to a direct A → B route, not fail.

---

## 2. How NPC movement actually works in LiF:YO

Every AI creature runs a **behavior tree** — an XML file (42 node types) ticked each frame; each node returns success / failure / running and composites decide what to evaluate next. The system is fully TorqueScript-exposed, so trees can be registered and hot-reloaded without touching the binary:

| TorqueScript function | RVA | Purpose |
|---|---|---|
| `initBehaviorsManager()` | `0x259F0` | Construct the `BehaviorsManager` singleton; runs once at boot. |
| `registerBehavior(name, fileName)` | `0x25AF0` | Bind a behavior-tree XML to a logical name (primary extension point). |
| `reloadBehaviorXml()` | `0x25B70` | Hot-reload all registered XMLs; no restart. |
| `showBehaviorTrees()` | `0x25BF0` | Print all loaded trees (debug). |
| `printBehaviorTree(name)` | `0x25A70` | Print a specific tree (debug). |

Movement / combat primitives that matter:

| Node | Does |
|---|---|
| `GoToPosition` | Pathfind to a destination (navmesh). |
| `ChaseEnemy` / `Flee` | Path toward / away from a target. |
| `Attack` | Execute attack ability on current target. |
| `Damaged`, `ThreatValueChange`, `SetAggressionState`, `HasTarget`, `EnemyInRange` | The aggression state machine. |

**"Engage when attacked" is essentially free** — it is exactly how wolves work today (`Damaged` → raise threat → `SetAggressionState aggressive` → `ChaseEnemy` / `Attack`). The caravan tree is modeled on `cmAiWolf.xml`.

### Two real limitations

1. **No native waypoint node.** Behavior trees are *shared prototypes*, so a per-caravan route can't be baked into the XML. Per-route state must live on the creature instance or in a script-side controller.
2. **The engine pathfinder is slope-aware, not road-aware.** Its A* is closed-source. "Follow roads" is therefore not free.

---

## 3. The insight that makes road-following feasible

Roads in LiF:YO are **terrain substances**, not placed objects:

| Substance | `ter2_id` | Wheel speed bonus |
|---|---|---|
| StoneRoad | `177` | +10% |
| SlateRoad | `180` | +30% |
| MarbleRoad | `181` | +20% |

Per-tile substance lives in MySQL **`geo_patch.Substance`**, keyed by `TerID` + `GeoDataID`. It is fully queryable. So instead of reverse-engineering the engine A*, we:

1. Run **our own substance-weighted A\*** over the road-tile graph from `geo_patch` to produce a **waypoint chain** that hugs roads (true road cost — roads cost less, off-road costs more).
2. Let the engine's stock `GoToPosition` walk each short segment between adjacent waypoints (it handles local terrain / obstacle avoidance).
3. A thin **controller** advances the cart-horse waypoint-by-waypoint and resumes the route after combat.

This isolates the hard problem (road cost) into an A* over **static** data and keeps the runtime thin — no engine navmesh modification.

> **Roads are player-built.** A fresh world has none. When no road path exists between A and B, the caravan falls back to a direct route — matching "follow roads *if there are any*".

---

## 4. AI behavior-tree node ABI (the surface the caravan node is built on)

RE'd from `ddctd_cm_yo_server.exe` (image base `0x140000000`; VA = base + RVA). These are the facts `WalkWaypoints` reuses; the `LifxLogNode`/`TimeOfDayBetween`/`GoToPoint` nodes (PRs #120/#122/#124) already exercise all of them at runtime.

### 4.1 Node factory (prototype pattern)

The factory is a lazy singleton holding XML-class-name → prototype `INode*`; the engine clones a prototype for every `<node class="..."/>`.

| Symbol | RVA | CmOffset | Signature / notes |
|---|---|---|---|
| `getNodeFactory` | `0x153860` | `AI_GET_NODE_FACTORY` | `void* __fastcall()` — lazy singleton; returns the static factory at `0x140b7c5a0`. Idempotent, independent of `BehaviorsManager`. |
| `registerNode` | `0x153950` | `AI_REGISTER_NODE` | `void __fastcall(factory, const char* name, void** proto)` — inserts/replaces; **moves ownership** (`*proto` read then zeroed); stores prototype at factory-entry `+0x20`. |
| `createByName` | `0x153760` | `AI_CREATE_BY_NAME` | `void* __fastcall(factory, void** out, name)` — clones the stored prototype via its **vtable slot 2** (`call [vtbl+0x10]`). |
| `_createNode` | `0x153D20` | — | XML → node: reads `class`/`name`/`decorator`, calls `createByName`, then the clone's **slot 1** loader with the `TiXmlElement*`. |
| behavior-XML loader (hook trigger) | `0x153B80` | `AI_LOAD_BEHAVIOR_XML` | `void* __fastcall(self, const char* file)` — logs "Loading AI behaviour from [%s]". **Registration hook point.** |
| TinyXml1 attr getter | `0x45A920` | `AI_TIXML_ATTRIBUTE` | `const char* __fastcall(TiXmlElement*, const char* name, int /*0*/)` — the node receives a TiXml1 element, so a loader must use this, NOT LiFx's tinyxml2. |
| `reloadBehaviorXML` (manual console reload) | `0x1506D0` | — | The console-command path only; **boot does not call it** (boot's `onServerCreated` calls `0x153B80` directly). Register on `0x153B80`, retried until built-ins exist, so the custom node is present before any tree is parsed. |

### 4.2 `INode` vtable (6 slots, `__fastcall`, `this` in rcx)

| Slot | Offset | Method | Notes |
|---|---|---|---|
| 0 | `+0x00` | `void* dtor(this, unsigned flags)` | `flags&1` ⇒ `operator delete`. **Class-specific layout** (string at `+0x30`, child vector `+0x40/+0x50`); sizes vary (saw `0x40` and `0x58`), NOT a flat `0x48`. |
| 1 | `+0x08` | `bool load(this, TiXmlElement*)` | reads the `value` attr; shared no-op stub `0x14009de40` (`mov al,1; ret`) for no-param nodes. |
| 2 | `+0x10` | `void clone(this, INode** out)` | deep-copies; sets the new object's vtable to the **concrete class** vtable. |
| 3 | `+0x18` | `int process(this)` | per-tick. **Single arg (`this`)**; AI context reached via `getAiData` (`0x1530D0`). |
| 4 | `+0x20` | base method (`0x1531a0`/`0x1531c0`) | shared across classes. |
| 5 | `+0x28` | base method (`0x152fd0`) | shared across classes. |

`process` return codes: **`1` = success**, **`2` = failure**, **`3` = running**.

**Clone gotcha:** clones get the concrete class's hard-coded vtable, so patching a prototype's vtable copy does NOT change ticked instances — **slot 2 must be overridden** so clones adopt our vtable. The safe, layout-agnostic technique used by the shipped nodes: delegate to the template class's own `clone`, then overwrite the result's vtable pointer with our patched copy (slot 2 = our clone, slot 3 = our process; slots 0/1/4/5 kept from the template so dtor/load/base methods stay correct for the template-shaped object).

### 4.3 AI context (`getAiData`) + movement — the Phase-0 blocker, now resolved

`getAiData()` (`0x1530D0`) just returns `*(void**)(node + 0x28)` — **every node carries a pointer to its creature's AI context (the blackboard) at `+0x28`.** This is the resolved answer to the original Phase-0 unknown #3: it is how a custom node identifies *which* creature it is ticking. (`0x1530D0` is **not** a named `CmOffset` constant; the shipped node code inlines `node+0x28` directly. Verified in `hooks/ai/ABI_NOTES.md` and `hook_behavior_node.cpp`.)

`GoToPosition::process` (`0x2E5C70`) is the canonical "path to a point" action and the verified template for movement nodes:

- Gets the creature from the blackboard, virtual-calls `getPosition` (`vtbl[0x280]`) → current position.
- Destination is stored **on the node**: x at `+0x40`, y at `+0x44`, z at `+0x48`; a "moving" flag byte at `+0x4c`. A `GoToPosition`-shaped node is therefore larger than the `0x48` base — **do NOT store a 3-float vec on a `0x48` leaf.**
- If `dist(creature, dest) <= arrivalRadius`: `MoveEngine_Stop` (`0x14014FE40`) + return `1` (success). Else `MoveEngine_SetTarget(engine, &node+0x40)` (`0x14014FD50`) + return `3` (running). The creature's movement engine comes from `0x2E3380(creature)`. Arrival can also be polled directly: `0x14014FD30(engine) == 1`.

**Creature lookup is class-keyed (the gotcha that broke the first attempt).** The creature is not reached directly — it is boxed in the blackboard (a string→handle hash map) under a key naming the creature **class**: `"animal"` for wild animals, `"npcbase"` for human NPCs. The built-in `GoToPosition::process` is **hard-wired to `"npcbase"`**, so reusing it verbatim fails on an animal (`npc is nullptr`). **Our cart-horse is an animal**, so a custom node is mandatory.

Lookup helper: `bbFind(aidata, &stdString)` (`0x190D70`) returns the boxed handle; `**handle` = creature. Build the key `std::string` with the engine String ctor `0x454FA0(&buf, "animal")` and destroy with `0x86D60(&buf)`.

**`GoToPoint` (PR #124, verified):** clones `GoToPosition` for its layout, overrides slot 1 (parse `value="x y z"`) and slot 3 (own process). The process tries `"animal"` **then** `"npcbase"` (so it works for animals AND human NPCs), gets the engine via `0x2E3380(creature)`, and on the moving flag: set target `0x14014FD50(engine,&dest)` / poll arrival `0x14014FD30(engine)==1` / stop `0x14014FE40(engine)`; returns `3` (moving) / `1` (arrived). Verified: a grouse pathfound to a literal coordinate with arrival detection. `WalkWaypoints` is `GoToPoint` plus a per-creature route + advance-on-arrival.

---

## 5. Plan

### Phase 0 — Research spike (mostly closed)

PR #120 answered "can we register a custom node?" (yes). The four original unknowns and their current state:

1. **Tile → world coordinate mapping** — how `GeoDataID` encodes tile row/col within a `TerID`, tile size, and `TerID` → world origin. Needed for A* adjacency and to emit world-coordinate waypoints. **Still open** (the one remaining Phase-0 item).
2. **Headless spawn-at-coords** — confirm a server-side `AnimalData` (cart-horse) can be placed at arbitrary coords. Use `Animals::Manager::createAnimal` (`0x195FD0`, `CREATE_ANIMAL`) on the manager singleton (`0xB80C90`, `ANIMALS_MANAGER_GLOBAL_RVA`); datablock by type via `AnimalData::GetDatablockByTypeID` (`0x18C790`, `ANIMAL_GET_DATABLOCK_BY_TYPE`). **Path known** (see [`ai_and_spawning.md`](ai_and_spawning.md)).
3. **`getAiData()` return struct (`0x1530D0`)** — what a node sees at tick: creature handle + position + move-target. **Closed** — it is `*(void**)(node+0x28)`, the AI-context blackboard (§4.3).
4. **`GoToPosition` mechanism** — how the move-target is stored/read and whether arrival is observable. **Closed** — dest on node `+0x40..+0x48`, moving flag `+0x4c`, set-target/poll/stop via the move engine (§4.3).

**Gate:** resolve unknown #1 (tile→world mapping) before emitting waypoints; the rest are answered.

### Phase 1 — Road pathfinder (offline / standalone)

- Input: start A, end B world coords.
- Query road tiles: `SELECT TerID, GeoDataID, Altitude FROM geo_patch WHERE Substance IN (177,180,181)`.
- Build a tile adjacency graph; edge cost = `1 / WheelSpeedMultiplier` for road, high for off-road, impassable for blocked/steep.
- A* from nearest-road(A) to nearest-road(B); prepend/append A and B connectors. **Fallback to direct A → B if no road path.**
- Output: simplified ordered waypoint list (world x,y,z) → a file the runtime loads.
- A standalone tool (Python or server `.cs`) over static data — no engine coupling.

### Phase 2 — LiFx DLL surface (built on PR #120's scaffolding)

Added via DLL injection / Detours — **never exe byte-patching** (project rule; see [`conventions.md`](conventions.md)). Reuse PR #120's node-registration hook (the `0x153B80` XML-loader hook + `std::call_once`), the `INode` vtable ABI, and the `hook_behavior_node.{h,cpp}` template.

- **`WalkWaypoints` custom node** — a leaf sibling of `GoToPoint`. Each tick: read this creature's route + progress from a **creature-keyed side table** (the creature is resolved via `getAiData()` → blackboard `bbFind` with `"animal"`), set the move-target to the current waypoint, return `running` (`3`) until arrival, then advance; return `success` (`1`) at the final waypoint.
  - The route is stored **outside the node**, in the side table — PR #120's clone trick clones a stateless `0x40` template and can't safely carry extra per-node fields, and the route is per-caravan anyway. Clone from `GoToPosition` (richer `0x40..0x4c` layout) so the engine's move plumbing is inherited.
- `caravanSpawn(datablock, x, y, z, behaviorName)` — spawn the cart-horse (an `AnimalData`) at coords via `createAnimal`, attach the tree, and register its route in the side table.
- `caravanGetPos(npc)` / arrival check — may be unnecessary if the node owns advancement (the engine already exposes `getPosition` `vtbl[0x280]` and arrival poll `0x14014FD30`).

### Phase 3 — Behavior tree + caravan controller

- **Tree** `cmAiCaravanHorse.xml`, modeled on `cmAiWolf.xml`: combat branch (defend when `Damaged`) at top priority; travel branch (`WalkWaypoints` toward the current waypoint) at the bottom.
- **Controller** — a TorqueScript `ScriptObject` in a new `CaravanMod` package (following the `JorvikModv2` pattern): loads the waypoint file, calls `caravanSpawn` at A (cosmetically harness a `HarnessedHorseCart`, datablock id **`694`**), and **monitors** the run. Waypoint advancement lives in the `WalkWaypoints` node, so the controller mainly listens for arrival / combat events and despawns or fires an event at B. (Thinner than the original script-tick-driver design, thanks to the custom node.)

### Phase 4 — Deferred: escort group

Guard NPCs that `FollowOwner` the cart-horse and share aggression (all turn hostile when any caravan member is attacked). Separate PR. Note that human guards are `"npcbase"`-keyed, with the native animal AI nodes hard-gated to `Animals::Animal` — see [`hostile-npc-ai-path-comparison.md`](hostile-npc-ai-path-comparison.md) for the two implementation paths.

---

## 6. Verification plan

- **Phase 0:** spawn a test cart-horse at known coords; confirm position in-world; confirm `GoToPoint` moves it to a literal coordinate and reports arrival (already demonstrated on a grouse for PR #124).
- **Phase 1:** run the pathfinder on a known paved stretch; verify output waypoints lie on road tiles; verify a no-road A → B falls back to direct.
- **End-to-end:** watch the cart-horse travel A → B hugging a road; attack it mid-route; confirm it goes `aggressive`, fights, then resumes travel from where it left off (observe via `printBehaviorTree` / logs).

---

## 7. Risks / open questions

- **Tile→world mapping is the last Phase-0 item.** Without it, A* adjacency and waypoint emission can't be exact. Everything else in the AI surface is RE'd.
- **No roads = no road-following.** On a young server with little paved road, caravans will mostly travel direct lines. Acceptable for launch, or seed some roads first?
- **Performance:** A* is offline over static road data, so runtime cost is low. Route recompute is only needed when roads change.
- **Multiplayer visibility / handoff:** a long A → B route may cross sector boundaries — interaction with the sector-handoff work is untested and may need coordination (see [`sector_handoff_design.md`](sector_handoff_design.md)).

---

## 8. Reference (for engineers)

- AI node catalog + registration recipe: [`ai_and_spawning.md`](ai_and_spawning.md)
- AI node ABI RE notes: `source/server/hooks/ai/ABI_NOTES.md`; shipped node code: `source/server/hooks/ai/hook_behavior_node.cpp`
- Trees to model on: `lif_server_320850/data/ai/cmAiWolf.xml`, `cmAiDomesticHorse.xml`
- Road substances: `extra/lif_client_qt515_spike/scripts/cm_substances.cs:1446`
- DB schema (`geo_patch`, `terrain_blocks`, `movable_objects`): `lif_server_320850/sql/new.sql`
- Spawn / behavior commands: `extra/lif_client_qt515_spike/scripts/npc_launch.cs`
- Cart datablock (HarnessedHorseCart `694`): `extra/lif_client_qt515_spike/art/datablocks/Transport.cs`
- Mod package pattern: `lif_server_320850_c/mods/JorvikModv2/mod.cs`

---

## Status & provenance

**status: design** — the caravan event is a proposal/plan; no caravan code has shipped. The *primitives* it stands on are not speculative:

**Runtime-verified (merged PRs #120/#122/#124, confirmed on the `_c` shard):**
- Custom behavior-tree node registration via the `0x153B80` XML-loader hook.
- `getAiData()` (`0x1530D0`) = AI-context blackboard pointer at `node+0x28`.
- `GoToPosition::process` (`0x2E5C70`) movement template, `bbFind` (`0x190D70`) class-keyed creature lookup, and the move-engine set-target / poll / stop trio (`0x14014FD50` / `0x14014FD30` / `0x14014FE40`); a grouse pathfound to a literal coordinate with arrival detection via the `GoToPoint` node.
- Node-factory + ABI offsets (`getNodeFactory 0x153860 = AI_GET_NODE_FACTORY`, `registerNode 0x153950 = AI_REGISTER_NODE`, `createByName 0x153760 = AI_CREATE_BY_NAME`, loader `0x153B80 = AI_LOAD_BEHAVIOR_XML`, attr getter `0x45A920 = AI_TIXML_ATTRIBUTE`) cross-checked against `source/server/cm_offsets.h`; the AI namespace and TS-exposed reload functions cross-checked against `docs/ai_and_spawning.md`. Animal-spawn RVAs (`CREATE_ANIMAL 0x195FD0`, `ANIMALS_MANAGER_GLOBAL_RVA 0xB80C90`, `ANIMAL_GET_DATABLOCK_BY_TYPE 0x18C790`) are named constants in `cm_offsets.h`.

**RE'd but not yet runtime-exercised for this feature:** the road A* itself (Phase 1) and `WalkWaypoints` (Phase 2) are unbuilt; the **tile → world coordinate mapping (`GeoDataID`/`TerID`)** needed to emit waypoints is the one open Phase-0 unknown.

**Offset notes / non-constants:** `getAiData 0x1530D0` and the movement RVAs (`0x2E5C70`, `0x190D70`, `0x2E3380`, `0x454FA0`, `0x86D60`, and the `0x14014Fxx` move-engine trio) are **not** named `CmOffset` constants — they are inlined in the shipped node code and documented in `source/server/hooks/ai/ABI_NOTES.md`. No conflicts with `cm_offsets.h` were found; every overlapping value matched its named constant.
