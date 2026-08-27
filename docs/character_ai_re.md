---
title: Character AI & equip-over-ghost RE
status: re
domain: reverse-engineering
tags: [npc-ai, equip-over-ghost, packupdate, charstats, behavior-tree]
related: [ai_and_spawning.md, hostile-npc-ai-path-comparison.md, character_hp.md, net_events.md, netevent_abi.md]
sources: [source/server/cm_offsets.h, source/server/hooks/ai/ABI_NOTES.md, source/server/hooks/ai/VERIFICATION.md, source/server/hooks/ai/hook_behavior_node.cpp, docs/ai_and_spawning.md, docs/hostile-npc-ai-path-comparison.md]
updated: 2026-06-26
---

# Character AI & equip-over-ghost RE

The "A2a" goal: a hostile NPC that is character-backed (fights, equips real
armor, drops the worn loot on death) *and* AI-driven by a behaviour tree. The
core finding is that those two halves don't combine on a raw `Player`: a
`Player` has the character/equip/death pipelines but **no AI tree slot, no
move-engine, no AI tick**; a bare AI creature has the tick but no character
backing. The resolution is `NPCS::NPCDecorative` — whose `NPCS::Base` ctor
writes **both** layouts — driven by custom blackboard-keyed nodes, network-
ghosted by hand, with armor painted onto the ghost by a `packUpdate`/
`unpackUpdate` bolt-on. Autonomous movement, patrol, equip-over-ghost render,
and the charStats→`CmPlayerEquipment` bind are all runtime-verified; combat
(taking damage / killable / worn-loot drop) is the remaining gap.

All RVAs are relative to image base `0x140000000` (server `ddctd_cm_yo_server.exe`
unless tagged **(client)** = `yo_cm_client.exe`, same base). Constant names in
**SMALL-CAPS-LIKE** form are the named `CmOffset` in `source/server/cm_offsets.h`.

## 1. The two-halves problem and the fork resolution

| Entity | Character layout (charStats `+0xAA8`, equip, death→loot) | AI layout (tree `+0x24B8`, move-engine `+0x24C0`, per-frame tick) |
|---|---|---|
| `Player` (ctor `0x0E87B0` / `0x0E87B5`) | yes | **no** |
| `Animals::Animal` | no character/equip backing | yes (native nodes hard-gated to `Animals::Animal`) |
| `NPCS::NPCDecorative` | yes | yes |

Decisive RE: **`NPCS::Base::ctor` (`0x2E2C10`, `NPCS_BASE_CTOR`) writes both the
character layout (charStats `+0xAA8`, plus `+0x128`, `+0x1E30`) AND the AI layout
(tree `+0x24B8` = `NPC_AI_TREE_OFF`, move-engine `+0x24C0` = `NPC_MOVE_ENGINE_OFF`,
plus move params at `+0x24C8..`).** A raw `Player` (ctor `0x0E87B5`) has the
character half but not the AI half. Every `NPCS::*` class therefore already
carries *both* — it is the entity that can be AI-driven and carry equip/death/
loot.

**Chosen path = drive an `NPCDecorative`/`NPCS::PlayerBased`, not a raw Player.**
Path A (AI-drive a raw Player by synthesizing a `Move` struct into
`Player::updateMove 0x106040`, vtable slot 176, each tick with connectionless
injection) was rejected as high-risk — `getEngine 0x2E3380` is literally
`*(creature+0x24C0)`, which is NPC-only; on a Player `+0x24C0` is garbage, so
`GoToPoint`/`ChaseEnemy` won't move it and may corrupt memory.

> `NPCDecorative` is an `NPCS::Base`, **NOT** an `AnimatedNPC`. Native
> `AnimatedNPC`-gated nodes (`PlayAnimationTimed`, the animal-AI movement nodes,
> and the native `EnemyInRange`/`ChaseEnemy`/`Attack`/`ThreatValueChange` combat
> nodes) fail on it; gate-less custom `"npcbase"`-keyed nodes work. See
> [`hostile-npc-ai-path-comparison.md`](hostile-npc-ai-path-comparison.md) for
> the Path 1 (re-platform onto `Animals::Animal`) vs Path 2 (custom npcbase
> nodes) decision.

## 2. Class hierarchy, object layout, vtables

```
Player(ctor 0x0E87B0) -> NPCS::Base(ctor 0x2E2C10)
   -> NPCS::PlayerBased(ctor 0x2E5210, entity vtbl RVA 0x7E5638)
      -> NPCS::NPCDecorative(ctor 0x2E46B0, entity vtbl RVA 0x7E3D08)
```

Object size **`0x2518`**. `NPCS::PlayerBased` (entity vtbl `0x7E5638`) is the
shipped "NPC built on the Player interface" — the closest engine precedent.

`NPCDecorative`'s entity vtable is **Player's vtable shifted +3 inserted slots,
reusing Player's own functions**: the death→corpse→worn-loot pipeline is already
wired in (it inherits `spawnLootstone`, `onDeathHappens`, `updateMove`).

| Constant / object | RVA | Notes |
|---|---|---|
| `NPCDEC_VTABLE` (entity primary vtbl) | `0x7E3D08` | gate: `*(void**)this == base+0x7E3D08` |
| `NPCS::PlayerBased` entity vtbl | `0x7E5638` | |
| `Animals::Animal` entity vtbl (`ANIMAL_VTABLE`) | `0x798AA0` | |
| charStats vtbl — base | `0x738A80` | the `+0xAA8` subobject |
| charStats vtbl — Player | `0x75CC78` | |
| charStats vtbl — `NPCS::Base` | `0x7E2F58` | |
| charStats vtbl — `PlayerBased` | `0x7E5CB8` | |
| charStats vtbl — `NPCDecorative` | `0x7E4388` | live-confirmed on a spawned NPC (id 1875) |
| `CmCharacterInfo` vtbl | `0x7A3D68` | returned by `charStats.vtbl[+0x40]` |
| `CmPlayerEquipment` slotDB vtbl | `0x7AEB70` | `[slotDB+0]`; equip itself is non-polymorphic |
| (client) `NPCDecorative` primary vtbl | `0xF7BC08` | gate for the client unpack hook |

### Vtable slot map (entity vtable, `__fastcall`, `this` in rcx)

| Slot | Offset | Method | RVA | Constant |
|---|---|---|---|---|
| 44 | — | `spawnLootstone` | `0x102570` | `SPAWN_LOOTSTONE` |
| 48 | — | `onDeathHappens` | `0x0FB390` | `ON_DEATH_HAPPENS` |
| 54 | `+0x1B0` | `packUpdate` (NPC thunk) | `0x2E54C0`→`0x0FC8B0` | `NPCDEC_PACKUPDATE_SLOT=54`, `NPCDEC_PACKUPDATE_THUNK` / `SHAPEBASE_PACKUPDATE` |
| 55 | `+0x1B8` | `unpackUpdate` (NPC thunk) | `0x2E5500`→`0x103940` | — |
| 104 | `+0x340` | `_applyHit` | `0x2E2E60` (`Player` `0x0EE0F0`=`PLAYER_APPLY_HIT`) | — |
| 176 | — | `updateMove` | `0x106040` | shared Player+NPC |

> **Slot-number caveat (flagged conflict).** Two numbering schemes appear in the
> source notes. `cm_offsets.h` and the shipped hook use **slot 44**
> (`spawnLootstone`) / **slot 48** (`onDeathHappens`) / **slot 54**
> (`packUpdate`). An earlier RE note counted the same functions as Player slot
> **249** / **253** / and `+3`-shifted NPC slots **252** / **256**. The RVAs
> (`0x102570`, `0x0FB390`, `0x0FC8B0`) are identical either way; trust the RVA,
> not the index. An earlier note also mislabeled slot 104 as `packUpdate` — that
> was wrong; **slot 104 = `_applyHit`** (assert strings `NPCS::Base::_applyHit`
> @ `0x2E2E60`, `Player::_applyHit` @ `0x0EE0F0`), and `packUpdate` is slot 54.

## 3. The AI movement stack

There is **NO central AI loop / `BtWorld` AI tick** — `BtWorld` is the Bullet
*physics* world. The behaviour-tree tick is a **per-class virtual**:
`Animals::Animal::packUpdate` (`0x18B450`) calls `AiTree::process`
(`0x154020`, `AI_TREE_PROCESS`) to tick the tree. The live tree lives at
`creature+0x24B8`, the move-engine at `creature+0x24C0` — offsets baked into the
`NPCS::Base`/`NPCDecorative` (`"npcbase"`) layout that a `Player` does not have.
`NPCDecorative`'s own pack does NOT call `AiTree::process` (engine slaves stay
static), so AI-enabled NPCs need the tree ticked another way (`setActive`, §5).

`getEngine(creature)` = `0x2E3380` = literally `*(creature+0x24C0)`. Movement
primitives on that engine:

| Op | RVA | Note |
|---|---|---|
| set target | `0x14FD50` | `MoveEngine_SetTarget(engine, &dest{x,y,z})` |
| poll arrival | `0x14FD30` | returns `==1` on arrival |
| stop | `0x14FE40` | `MoveEngine_Stop(engine)` |
| `AI::MovementEngine` | `0x14F8B0` | |
| `getAIMove` | `0x14F880` | |

### Blackboard (AI context) and the class-keyed resolver

`getAiData()` (`0x1530D0`) returns `*(void**)(node+0x28)` — every behaviour node
carries a pointer to its creature's AI context (blackboard, a string→handle hash
map) at `+0x28`. The blackboard boxes the creature as `SimObjectPtr<T>`, and the
find is a **per-T template that RTTI-type-GATES**, so each key must be paired
with its matching find:

| Key | Boxed type | Find RVA | Written by |
|---|---|---|---|
| `"animal"` | `SimObjectPtr<Animals::Animal>` | `0x190D70` | Animal context-setup `0x18AD00` |
| `"npcbase"` | `SimObjectPtr<NPCS::Base>` | `0x190E90` | NPC tree-attach `0x2E38D0` (`TREE_ATTACH`) |

`"player"`/`"self"` exist as strings but **nothing populates them**. Resolve a
handle with `*(*handle)` (`== 0x191130`). `bbInsert = 0x189420`; build the
`std::string` key via String ctor `0x454FA0(&buf, "...")` and destroy with
`0x86D60(&buf)`. **Gotcha:** a node that uses the Animal find (`0x190D70`) for a
`"npcbase"` key returns null on NPCs — pair each key with its own find.

### Custom node ABI (the `GoToPoint` model)

Node factory is a prototype/clone pattern (full RE in
`source/server/hooks/ai/ABI_NOTES.md`):

| Symbol | RVA | Constant |
|---|---|---|
| `getNodeFactory` (lazy singleton @ `0x140b7c5a0`) | `0x153860` | `AI_GET_NODE_FACTORY` |
| `registerNode(factory, name, INode** proto)` | `0x153950` | `AI_REGISTER_NODE` |
| `createByName(factory, INode** out, name)` (calls proto vtbl slot 2) | `0x153760` | `AI_CREATE_BY_NAME` |
| `_createNode` (XML→node) | `0x153D20` | — |
| per-tree XML loader (registration hook trigger) | `0x153B80` | `AI_LOAD_BEHAVIOR_XML` |
| TinyXml1 attribute getter `Attr(elem,name,0)` | `0x45A920` | `AI_TIXML_ATTRIBUTE` |
| `BehaviorsManager::reloadBehaviorXML` (console reload only) | `0x1506D0` | — |

INode vtable = 6 slots: `0 dtor`, `1 load(this, TiXmlElement*)` (no-param stub
`0x9DE40` = `mov al,1; ret`), `2 clone(this, INode** out)`, `3 process(this)`
(single arg; AI context via `getAiData 0x1530D0`), `4` base (`0x1531A0`/
`0x1531C0`), `5` base (`0x152FD0`). `process` return codes: **1 = success,
2 = failure, 3 = running**.

Register from `0x153B80` (per-tree loader) — NOT `0x1506D0` (boot never calls
it). Clones get the **concrete class's hardcoded vtable**, so patching a
prototype vtable copy does nothing to ticked instances: override **slot 2** so
clones adopt our vtable. Safe technique: delegate to the template class's own
`clone`, then overwrite the result's vtable pointer with our patched copy
(slot 2 = our clone, slot 3 = our process; keep slots 0/1/4/5 from the template).

`GoToPosition::process` (`0x2E5C70`) is the canonical movement node and the
layout model: dest x/y/z at node `+0x40`/`+0x44`/`+0x48`, a "moving" flag byte
at `+0x4c` (so a movement node is larger than the 0x48 leaf base — don't store a
vec on a flat 0x48 leaf). It is hard-wired to `"npcbase"`. Our shipped
`GoToPoint` clones it, parses `value="x y z"` in slot 1, and in slot 3 tries
`"animal"` then `"npcbase"` so it works for both. See
[`ai_and_spawning.md`](ai_and_spawning.md) for the full 42-node catalog.

## 4. Time-of-day source (used by AI day/night nodes)

`getGameTime()` (EngineFunction impl `0x4691A0`) reads a 64-bit microsecond game
clock. The world/time singleton ptr is `*(base+0xB7E4C0)`; game time =
`*(int64*)(obj+0x10)`; `hour = (T / 3'600'000'000) % 24`. Confirmed accelerated
in-game (not wall-clock): node read `hour=10`→`11` ~9 real-minutes apart.
`0x468760` is the value source; `0x2DA1C0` wraps it with `±inf`/sentinel
handling (irrelevant for a live clock).

## 5. Autonomous movement / patrol recipe (runtime-verified)

A player-model `NPCDecorative` ticks a tree and MOVES with no player nearby:

1. Datablock `behavior = "data/ai/<tree>.xml"` (a tree with a movement node;
   node classes register at startup via the `0x153B80` loader hook).
2. Spawn it, then call the native script method **`%npc.setActive(true)`**
   (`fnNPCDecorative_setActive`) → runs activate **`0x2E3460`**: builds the
   move-engine `+0x24C0` (owner-bound) + tree `+0x24B8` + binds the creature into
   the tree blackboard (tree-setter `0x2E38D0` inserts under key `"npcbase"`).
3. `NPCDecorative` is **TICKABLE from its ctor** (process flags
   `or [+0x10],0x40000000` / `or [+0x1B8],0x10000` at ctor `0x2E2C10`;
   advancePhysics/integrator = `0x2E3590`), so the engine ticks the tree +
   integrates locomotion every frame — no manual ticking.

Crash gotchas: a raw `new NPCDecorative()` is **not** activated → move-engine is
a ctor stub → ticking crashes; you MUST `setActive`. Swapping the tree via
`setBehavior` *after* activate leaves new nodes unbound ("no creature") — set
`behavior` on the **datablock** so activate binds the right tree. Calling
`AiTree::process 0x154020` yourself on an unactivated/stub tree crashes.

**Patrol** works tree-only: a `DynSelector` root over a `Sequence` of `GoToPoint`
waypoints loops continuously (the re-evaluating root restarts the sequence).
Working tree `data/ai/lifxWanderTest.xml` (4-waypoint box). Reload a tree edit
without restart: `reloadBehaviorxml()` then spawn a FRESH NPC (existing NPCs keep
their bound tree).

EngineAPI setters: `setActive 0x2E4B00`, `setBehavior` wrapper `0x2E4B60`
(real `0x2E4850` = `SET_BEHAVIOR`), `getBehaviorTree 0x150D30`
(`GET_BEHAVIOR_TREE`; mgr singleton @ `0xB7BEB8` = `BEHAVIOR_MGR_SINGLETON`).

## 6. Character backing — charStats → CmCharacterInfo → CmPlayerEquipment

A fresh `NPCDecorative` has the char layout fully live (live dump of id 1875:
charStats vtbl = `0x7E4388` MATCH; flag `+0x4A9 == 1`; equip cache present
@ `+0x498`; charID `+0x109C == 0`; back-ptr `charStats-0x728 == null`), but its
charStats is **not tied to a `character` row**, so equipment is null and the
engine logs-and-skips (not a crash): `"player %u has null CmPlayerEquipment"`,
`"...null CmCharacterInfo"`.

### Equipment resolution (FINAL, verified live on `_d`, charID 1)

Two independent pure-read routes return the SAME `CmPlayerEquipment*`:

- **FAST (cached):** `CmPlayerEquipment* = *(void**)(charStats + 0x498)`; its
  refcount ctrl-block = `*(charStats + 0x4A0)`.
- **SOURCE OF TRUTH:** `charStats.vtbl[+0x40]() → CmCharacterInfo` (vtbl
  `0x7A3D68`); then `CmCharacterInfo + 0x370` (`CCI_RECORD_OFF`) = **record**
  (`+0x378` = record refc); then **`record + 0x48`** (`RECORD_EQUIP_OFF`) =
  `CmPlayerEquipment*` (`record + 0x50` = `RECORD_EQUIP_REFC_OFF` = its refc).
  Cache-or-rebuild getter = **`0x0A12E0(charStats, void** out)`** (acquires a
  ref; verified instruction-by-instruction).

Inside `CmPlayerEquipment` (0x70 bytes; non-polymorphic — `[equip+0]` is a heap
ptr, not a vtable): `+0x50` = slot count (`EQUIP_PLAYER_ID_OFF` also = CharacterID
/ player_id here, U32, live value 1); `+0x58` = slot-DB / root container
(`EQUIP_CONTAINER_OFF`, polymorphic — `[slotDB+0]` is a real vtable `0x7AEB70`);
container `+0x100 + slot*8` = slot's itemId (`EQUIP_SLOT_BASE_OFF`, slots
1..0x11). Mutators (all take `CmPlayerEquipment* this`):

| Fn | RVA | Constant |
|---|---|---|
| `applySlotChanges` | `0x1F00B0` | `EQUIP_APPLY_SLOT_CHANGES` |
| `canSetSlot` | `0x1F01D0` | `EQUIP_CAN_SET_SLOT` |
| `_setSlotDB` (persists slot to SQL `equipment_slots`) | `0x1EEA40` | `EQUIP_SET_SLOT_DB` |
| `_setDefaultMeshesHidden` (mesh-cull recompute) | `0x1F0660` | `EQUIP_SET_DEFAULT_MESHES_HIDDEN` |
| high-slot mutate `setSlot(equip,u8 slot,u64 itemId(+skinId hi32),int)` | `0x1F38D0` | `EQUIP_SET_SLOT_HIGH(_FN)` |
| `loadFromDb` (populate slots from `equipment_slots`) | `0x1F2760` | `EQUIP_LOAD_FROM_DB` |

> **Note (two different functions, not a conflict):** an earlier note wrote
> "`_setSlotDB 0x1EE930`". `0x1EEA40` (`EQUIP_SET_SLOT_DB`) persists a slot to
> SQL; `0x1EE930` is the lower-level `_setSlot` that writes the itemId into
> `container+0x100+slot*8`. They are distinct; both names have appeared for the
> SQL one in older notes.

> **Do NOT mutate the bound equip in place** (`applySlotChanges`/`_setSlotDB`/
> `0x1F38D0`/`_setDefaultMeshesHidden`) when the charStats is bound to a *live*
> player's charID — the `CmPlayerEquipment` is **shared** with that player
> (`charStats+0x498`) and you corrupt the real player.

### The charID-getter sentinel gate (why NPCs get null equipment)

How a charStats resolves its `CmCharacterInfo` is fully decoded. The getter
`charStats.vtbl[+0x40]` is the SAME fn `0x0A11C0` for every charStats vtable
(Player `0x75CC78`, NPCDecorative `0x7E4388`, `NPCS::Base` `0x7E2F58`,
`PlayerBased` `0x7E5CB8`, base `0x738A80` — slot `+0x40` identical). Logic:

```
if (charStats[+0x4A9] == 0) return null;
charID = charStats.vtbl[+0x08]();            // the ONLY per-class-overridden slot
if (!charID) return null;
return HashTable.find(manager *0x140B53908, charID);   // find = 0x28BC20 (FNV-1a)
```

The charID-getter `vtbl[+0x08]` is per-class:

- **Player `0x0F8B00`:** reads linked char-obj at `charStats-0x728` (tail-calls
  getId `0x134A80`), else falls back to `charStats+0x109C` (real per-instance
  charID).
- **NPCDecorative `0x2E3390`: `mov eax,0xFFFFFFFE; ret`** — hard-coded sentinel
  **−2**, no field. charID −2 isn't registered → getter returns null → "null
  CmCharacterInfo" → no equip/loot. **This is the root cause.**

So a charStats→CmCharacterInfo needs three things: (1) flag `charStats+0x4A9 != 0`
(a fresh NPCDecorative already has this = 1); (2) charID-getter returns a
non-zero key registered in the manager; (3) a `CmCharacterInfo` registered under
that key. Flag setter = `setCharacterBacked 0x95D40(rcx=cs, dl=+0x4A9,
r8b=+0x4AA)` — the only writer of `+0x4A9`; it also flushes the equip cache
(`+0x498=0`, releases `+0x4A0`) and calls vtbl `+0xC8`/`+0xD0`
(invalidate/notify). The manager is the global type/registry singleton
`*0x140B53908` (`EQUIP_REGISTRY_GLOBAL_RVA = 0xB53908`); `find 0x28BC20` is its
FNV-1a charID lookup (== `Engine::Character_GetByID`).

### Bind recipe (DLL-side, no exe patch) — CONFIRMED LIVE

Two strategies. **Strategy S (shared, simplest):** register ONE fully-built
`CmCharacterInfo` under key `0xFFFFFFFE` in manager `0x140B53908`, then
`setCharacterBacked(npc.charStats, 1, x)` on each NPC → all share one loadout;
getter `0x0A12E0` resolves equip for free. **Strategy P (per-NPC, CONFIRMED on
`_d`, NPC id 1875):** `<id>.lifxBindNpc(1)` resolved charID 1 to a real
`CmCharacterInfo`→record→`CmPlayerEquipment` (count `+0x50`=1, slotDB vtbl
`0x7AEB70`); it cloned the charStats vtable, patched slot `+0x08` to return a
per-instance charID (e.g. `charStats+0x109C`), wrote the charID, armed the flag,
and flushed the cache — **SUCCESS, the charStats→equipment data path is proven
end to end.**

Building a populated `CmCharacterInfo` (the `#145` worn-loot reuse):

| Step | RVA | Constant |
|---|---|---|
| field-init ctor (sets vftable, zeroes, charId @ `cci+0x358`, no DB) | `0x1B9C50` | `CCI_FIELD_INIT_CTOR` |
| full connect-FREE DB load (stats + inventory + equip + HP sync) | `0x1BB290` | `CHARACTER_LOAD_INMEM` |
| `CmInventoryPlayer::init(record, cci)` — allocs equip @ `record+0x48`, inv @ `record+0x40`, builds root container | `0x2900B0` | `CM_INVENTORY_PLAYER_INIT` |
| record ctor (0x58 bytes) | `0x28ECE0` | — |
| slotDB ctor / equip slot ctor | `0x1EC220` | — |
| `CmPlayerEquipment` ctor (zero-inits +0x50 count / +0x58 slotDB) | `0x1EC790` | — |
| get-or-create (alloc 0x3a8 → cci ctor → record accessor 0x1BEE70 → equip build → register) | `0x28CB20` | — |
| full DB-backed account+char+CmCharacterInfo | `0x1D29B0` | `CREATE_TEST_CHARACTER` |

Offline (no `GameConnection`) cci construction mirrors `0x28B050`:
`refc = ENGINE_ALLOC(0x3a8)` (`0x6DF950`); `*refc = base+0x7CFD98`
(`CCI_REFCOUNT_VFTABLE_RVA`); `*(u32*)(refc+8)=1; *(u32*)(refc+0xC)=1;
cci = refc+0x10`; `CCI_FIELD_INIT_CTOR(cci, charId)`; `CHARACTER_LOAD_INMEM(cci)`.
The cci ctor's signal/TLS registration is **main-thread-only** — marshal via
`schedule()` (`IS_MAIN_THREAD 0x407EF0`); console commands run on a worker
thread. `0x28CB20`'s registration key derives from manager state (read
@ `manager+0x1C0`) and is **not yet pinned**.

## 7. Network ghosting (GID acquisition)

A raw `new NPCDecorative()` never acquires a LiF **GID** (no `increased gid` /
`Sending new ghost` logs, unlike animals via `Animals::Manager::createAnimal`
`0x195FD0`). Ghosting in LiF is GID-gated; the GID-acquire log fn references the
format `"Object %s [%u](%s) increased gid [%s] usage to %u via %s"` @ `0x8C3FB0`.

**Fix (confirmed live `_d`): `NetObject::setScopeAlways` @ RVA `0x54AC70`** —
`((void(__fastcall*)(void*))(ModuleBase()+0x54AC70))(npcObj)`. Self-guarding:
no-op unless `(netFlags @ obj+0xDC & 0x12) == 0x10`; a fresh NPCDecorative has
`0xDC == 0x10` (IsGhostable set, ScopeLocal clear) → it sets ScopeAlways
(`|0x4 → 0x14`), gets a GID, and logs `Sending new ghost (NPCDecorative
classId=101) over ch_1`. Shipped as `%npc.lifxGhost()`. Net-object scope-mgr
singleton `*0x140BC8528`, connection-list `*0x140BC8558`, `objectInScope
0x5468A0`. Once positioned validly near a player the NPC also auto-ghosts via
normal proximity scope, so `lifxGhost` is a backup, not strictly required.

> An NPCDecorative is **TRANSIENT by design** — the engine reaps it when its last
> client ghost is freed (`freeGhostInfo` usage→0 on disconnect → `onRemove`). A
> bare `new NPCDecorative()` also lands in the per-session `MissionCleanup`
> group; reparenting to `MissionGroup` does NOT save it. The shipped fix is a
> self-rescheduling maintenance tick (`Lifx::manageSpawn`/`ensureSpawns`) that
> re-creates any node whose NPC is gone (keyed by `SimObjectId` via `isObject`,
> pointer-reuse-safe). **Never call `schedule()` from a ConsoleInit-time path** —
> the Sim event queue isn't up yet → crash, *before* the log file is even
> created. Start ticks lazily at runtime.

## 8. Equip-over-ghost bolt-on (the painting channel)

The NPC carries no `Player` ghost, so other clients see a naked body. The fix is
a hand-rolled equipment block appended to the NPC's `packUpdate` and read in the
client's `unpackUpdate`. **The engine ENFORCES bit-for-bit `pack == unpack`
symmetry** (checksum in `ghostReadPacket`); a 1-bit desync triggers
`"unpackUpdate did not match packUpdate for object of class NPCDecorative"` →
instant disconnect — so server-append and client-read MUST be lockstep, LSB-first.

### The hook mechanism — VTABLE-SLOT PATCH, not Detours (key lesson)

The intermittent ~40% world-load hang was **the hook mechanism, not the
payload**. Detours-prologue-patching the **shared** `ShapeBase::packUpdate`
(`0x0FC8B0` = `SHAPEBASE_PACKUPDATE`, called by every Player + ShapeBase) rewrote
the prologue and **raced world-load worker threads**.

**Rule learned: NEVER Detours-prologue-patch a hot/shared engine fn that runs
during world load. Patch the class-specific vtable slot instead** — a pointer
swap rewrites nothing, has no thread race, and is structurally NPC-only (no
runtime gate needed).

- **SERVER:** patch the NPCDecorative entity vtable **slot 54 (`+0x1B0`)**, which
  holds the NPC-only thunk `0x2E54C0` (`jmp → 0x0FC8B0`), to `OnPackUpdate`
  (calls original, then appends the equip block). `NPCDEC_PACKUPDATE_SLOT = 54`;
  the thunk RVA `0x2E54C0` (`NPCDEC_PACKUPDATE_THUNK`) is an **install-time
  sanity check only** — refuse to patch if the slot doesn't hold the verified
  thunk. Install post-console-init on the main thread.
  Files: `hook_npcdec_pack.{h,cpp}` (`InstallVtablePatch`).
- **CLIENT:** the new-ghost read dispatch calls unpackUpdate at vtbl **slot 56
  (`+0x1C0`)** (`mov rax,[obj]; mov rdx,conn; mov r8,stream; call [rax+0x1C0]`;
  target `0x2107A0` does the inline bitstream read). Patch NPCDec client
  vtbl[56] (`= 0x46EF10`, a `jmp`-thunk→`0x2107A0`; Player vtbl[56]=`0x2107A0`
  direct) to `OnUnpackUpdate`. `InstallVtablePatch()` runs from `OnConsoleInit`
  (NOT DllMain-attach — engine console calls there abort the load under the Wine
  loader lock). Files: `hook_equip_unpack.{h,cpp}`.

> **Red herrings that cost cycles:** client `0x20CA90` ("Player::unpackUpdate",
> slot 115) is a parent call, not the dispatch entry — patch slot 56, not 115.
> Client vtbl layout ≠ server (client slots 54/55 are stubs); always find client
> fns by string/fingerprint, not by server slot index. The shipped server hook
> ultimately gates on the entity vtable (`*(void**)self == base+0x7E3D08` =
> `NPCDEC_VTABLE`) rather than relying on the thunk being NPC-only.

### BitStream layout & primitives (same both sides)

`+0x10` buffer, `+0x18` bitPos, `+0x20` capacity **bytes** (×8 = bits), `+0x28`
overflow flag. Bits LSB-first: `byte = buf[bitPos>>3]; bit = (byte>>(bitPos&7))&1;
bitPos++`. `readFlag` is **inlined** in unpack (client replicates it inline — no
need to find a client readFlag fn).

| Primitive (server) | RVA | Constant |
|---|---|---|
| `writeFlag(rcx=stream, dl=bool)` → returns bool written | `0x0A3E80` | `BITSTREAM_WRITEFLAG` |
| `writeInt/writeRanged(rcx=stream, edx=value, r8d=min/0, r9d=bitCount)` | `0x0A3F10` | — |
| `readInt` | `0x0A2DF0` | — |

`packUpdate` signature: `(rcx=this, rdx=conn, r8=mask U64, r9=stream)` → U32
retMask. The shared impl `0x0FC8B0` already reads charStats `[rsi+0xAA8]` (calls
charStats vtbl `+0x260`, writeFlag's the result) — so the decorative ghost *does*
carry charStats data; open question whether the native equip block can be enabled
by a flag rather than hand-rolled. Concrete exemplars to copy:
`Animal::packUpdate 0x18B450`, `Horse::unpackUpdate` (xref @ `0x3DAB3F`).

### Wire format

Increment 1 (PROVEN, magic round-trip, 256+ updates, no disconnect): append
**`[1 marker bit + 8 magic bits]`**; client logs match, no desync.

Final shipped loadout wire (**route B**): **`marker(1) + loadoutId(8)`** (LSB-
first). The **client** owns the id→mesh-set tables (`lifx_loadouts.h`: id `0` =
plate `Full_Plate_*`, id `1` = leather `Novice_Leather_*`; names from
`cm_equipTypes.xml <mesh>`). The server keeps a per-NPC `obj→loadoutId` map
(mutex-guarded), assigned via the SimObject method `%npc.lifxLoadout(id)`
(`obj == OnPackUpdate self`), default 0. An earlier index-list design was
`marker(1) + count(8) + count×index(16)` where `index` = position in the client
`kLifxArmorMeshes` — superseded by the loadout-id form.

**Gate on bound NPCs** (charID `!= -2` sentinel) so the append doesn't fire for
world outpost-slave NPCs in scope. The append does NOT fire during connect/load
(no NPCDecorative in scope until spawn), so the handshake is safe.

Open loadout bugs: (1) `g_loadouts` keyed by raw ptr → a destroyed NPC's reused
address inherits its loadout; fix = reset on spawn or key by `SimObjectId`.
(2) a script-spawned NPC (`setScopeAlways`) is not persistently scoped → vanishes
for a client that reconnects without a server restart; fix = proper datablock/
outpost spawn or auto re-scope on connect.

## 9. Client equip-render internals (yo_cm_client.exe, md5 474c429b)

Armor renders TWO ways: **body/clothing = show/hide sub-meshes baked into the
character TSShape**; **held weapons = ShapeBase image slots** (separate path,
dispatcher `0x3657D0` (client), not yet isolated).

| Client fn / field | RVA / offset | Note |
|---|---|---|
| `TSShapeInstance::setMeshHidden(rcx=shape, rdx=meshName, r8b=hidden)` | `0x23DCF0` | class-agnostic; bottoms at `0x140b691c0` writing visByte `[meshObj+0x34]` |
| per-index apply | `0x140b691c0` | `(renderInst, idx, hidden)` |
| per-slot apply worker | `0x372790` | `(rcx=descriptor{body@+0x00, shape(TSShapeInstance*)@+0x18}, dl=flag, r8b=slot, r9=meshName**)`; null shape@+0x18 falls back to LOCAL player `0x2A67B0` |
| vis-bitfield | `[shape+0x1270]` | meshCount `[shape+0x127c]`, renderInst `[shape+0x11a0]` |
| name→idx dict | `[[[shape+0x448]+0x208]+0x18]+0x28` | |
| `EquipmentEvent::unpack` (single-slot delta) | `0x37A4C0` | type Nbits @ `+0x40`; if type0: equipped 1bit @ `+0x45`, slot 3bits @ `+0x44`; NO item id (resolved from body data) |
| `EquipmentEvent::process` | `0x379DE0` | targets LOCAL control object ONLY (`getControlObject [conn+0x2c0]`) — do NOT route through it |
| body equip-type table | `[body+0x118]` | typeID vectors `[body+0xe0]`/`[body+0xf8]`, active equip `[body+0x100]` |
| class-agnostic mesh loop (proof) | `0x140a96480` | runs setMeshHidden on arbitrary per-object shapes |

**PREREQUISITE:** the NPC must use the PLAYER model
`art/models/3d/mobiles/characters/male.dts` (PlayerData `shapeFile`) — armor =
baked sub-meshes that exist only in `male.dts`; the slave model can't show them.

**Cosmetic culls (all verified in-game).** A bare NPCDecorative has no appearance
data, so `male.dts` defaults EVERY variant visible. Cull list `kLifxEquipExtraHide`
(`lifx_equip_hide.h`): ALL `Male_Hair_*`, `Male_Beard_*`, `Male_Underwear_*` (the
underwear is a "skirt"), and **`Male_Antiseam`** (a body-seam filler strip that
shows as a waist band / fake "default chest" under THIN armor like leather —
plate's bulk hides it; the hardest mesh to find). OMIT `Full_Plate_Helmet_Add`
(visor-add → 2nd helmet). Mesh names found via
`strings male.dts | grep <X> | grep -v _DIFFUSE` (`_DIFFUSE`/`_NORMAL` =
materials, not meshes; hair/beard mesh names end `_cut`/`_up`). Swapping
`Male_Body_ALL` ↔ individual `Male_Body_v1_*` parts had no visible effect — the
thin-armor fix was hiding `Male_Antiseam`.

## 10. Real-player equip sync (contrast) — CmCharacterInfo NetEvent

Equipment for **real players** does NOT go through `packUpdate` — it goes via a
`CmCharacterInfo` NetEvent keyed by char-id:
`Character_SendChanges 0x1BC3D0(charInfo, mask, sendNow)` (`CHARINFO_SEND_CHANGES`)
ORs `mask` into `charInfo+0x2F8`; the event packer **`0x1C0A60` gates the
equip/slot-OID block on mask BIT 10 (`0x400`)** (`bt rdx,0xa`). See
[`net_events.md`](net_events.md) / [`netevent_abi.md`](netevent_abi.md) for the
NetEvent vtable layout. This is the path an NPCDecorative *lacks* (it has no
char-id-registered CmCharacterInfo by default), which is why the §8 bolt-on
exists.

## 11. Spawn recipe, datablocks & client deployment

**Use the engine script path, NOT the raw factory.** Factory
`NPCDECORATIVE_CREATE 0x2E4AB0` (no args) = `malloc(0x2518)` → ctor `0x2E46B0` →
refc++ → vtbl `+0x50` = `registerObject 0x4304A0`. But `registerObject` runs
`onAdd` (vtbl `+0xD8` = `0x2E54A0` → thunk `0x2E33B0` → `ShapeBase::onAdd
0x0FAE80`), which **fails + deletes the object if datablock (`obj+0x2040`) is
null** → a bare factory call returns a dangling ptr (UNSAFE). Spawn via the
engine's own `new NPCDecorative(){ dataBlock="..."; }` (Con::Evaluate) so the
engine sequences ctor→setDataBlock→register/onAdd→`addToScene 0x133880`+ghost.

| Symbol | RVA / offset | Constant |
|---|---|---|
| `setTransform` (vtbl `+0x258`, `MatrixF @ obj+0x278`) | `0x0F4830` | — |
| `setDataBlock` (`obj+0x2040` & `+0x3A0`) | `0x0FBBA0` | — |
| `Sim::findObject(name)` | `0x428D00` | — |
| id-alloc (id @ `obj+0x90`) | `0x4290D0` | `SIMOBJECT_ID_OFF=0x90` |
| `registerObject` | `0x4304A0` | — |
| `AnimatedNPC::setAnimation(self, nameHandle, u8)` | `0x2E2A90` | `ANIMATED_NPC_SET_ANIMATION` |

The `position` init-field does NOT stick (`onAdd` resets transform → object lands
at world origin `0,0,-76`); `%o.setTransform("x y z 0 0 1 0")` **after** create.

**Client deployment (critical):** LiF loads datablocks LOCALLY on each side
(matched by `id`); custom datablocks are NOT reliably network-transmitted. A
spawned NPCDecorative whose datablock id the client lacks **CRASHES the client
instantly** on `ghostReadPacket`. So a custom NPC datablock MUST be added to BOTH
server and the client's own `art/datablocks/npc/npc.cs` with a **byte-matching
id**. `NPC_player_male = NPCData:DefaultPlayerData, id 251`, `shapeFile=male.dts`
(free ids; `250=BanditData`; slaves `710-715`). `PlayerData` is accepted by
`setDataBlock` but FAILS `NPCDecorative::onAdd` (needs CharId) — must use
`NPCData`. Runtime-defined datablocks (Con::Evaluate) also don't transmit → crash;
define at startup only. Remove any stale `npc.cs.dso` so the `.cs` recompiles.
The real client is the Steam install
`.../Life is Feudal Your Own/`. Loaded NPCData on `_d`:
`NPC_slave_A..E`, `NPC_slave_Overseer` (`:DefaultPlayerData`, AI
`aiSlaveBase.xml`).

## 12. Combat / death / loot — wired vs. gap

The death→corpse→worn-loot pipeline is **already inherited** by NPCDecorative
(it reuses Player's `spawnLootstone 0x102570` / `onDeathHappens 0x0FB390`), so it
is "free" iff the NPC's bound charStats yields a non-null `CmPlayerEquipment`
(§6). What is NOT yet wired is making the NPC **take damage / be killable** so
that pipeline ever fires.

| Symbol | RVA | Constant |
|---|---|---|
| `Player::_applyHit` (landed-hit side) | `0x0EE0F0` | `PLAYER_APPLY_HIT` |
| `NPCS::Base::_applyHit` (slot 104, `+0x340`) | `0x2E2E60` | — |
| `Calc_hit_damage_distribution` → ONEPUNCHMAN damage calc | `0x0A4BF0` | `ONEPUNCHMAN_DAMAGE_CALC` |
| HP-apply step `(charStatsSubObj, &dmgPacket{hardHp i64@+0, softHp i64@+8, 1e6 scale})` | `0x090F60` | `HIT_APPLY_DAMAGE` |
| charStats death→lootstone+worn-loot trigger (`charStats.vtbl[+0x130]`, engine calls `(charStats,0)` in death router `0x3BE890`) | slot `+0x130` | `CHARSTATS_DEATH_TRIGGER_SLOT` |
| loot transfer chain | `0x102770 → 0x1F2DA0` | (from `SPAWN_LOOTSTONE`) |

The player *swing initiator* RVA is not yet pinned (`_applyHit` is the landed-hit
side). Hostile behaviour (detect→chase→attack) will likely need gate-less custom
`"npcbase"` nodes (like `GoToPoint`) because the native combat nodes hit the
`AnimatedNPC` type-gate. See [`character_hp.md`](character_hp.md) for the HP
read/write internals and [`hostile-npc-ai-path-comparison.md`](hostile-npc-ai-path-comparison.md)
for the node-platform decision.

## 13. Dead ends (do NOT revisit)

- **"equipment = FNV hash-map keyed at `charStats+0xB14`, record via singleton
  `*0x140B53908`" is WRONG.** `0x28BD30` (`EQUIP_ACCESSOR`) / `0x28BC20` are
  GENERIC `HashTable<U32,T*>::find`. Calling `find(singleton, charID)` returns a
  CmCharacterInfo (`+0x48` null) or a SQL-statement-cache obj, NOT equipment.
  `[entity+0xB14]` (getter `0x134A80`) is a char-id but neither `Player+0xB14`
  nor `charStats+0xB14` hold it. Passing charStats as the `find()` table arg
  crashes (`_d`) — garbage bucket count @ `+0x1C0` → OOB. The direct
  `charStats+0x498` route (§6) supersedes all of this; `CHARSTATS_EQUIP_KEY_OFF`
  (`0xB14`) was removed.
- Loot fn `0x1F2C80`'s arg2 is some other char-object — irrelevant now.
- AI-driving a raw `Player` (Path A) — rejected (no move-engine).

## Status & provenance

**Runtime-verified (in-game, user-confirmed):**

- Autonomous NPCDecorative movement + patrol (2026-06-18, PR #126) — `setActive`
  recipe, `DynSelector`/`GoToPoint` patrol loop, dual-key blackboard resolver.
- Custom behaviour-node ABI — `LifxLogNode` / `TimeOfDayBetween` / `IsNight` /
  `GoToPoint` ticked live on `_c` (2026-06-15), see
  `source/server/hooks/ai/VERIFICATION.md`.
- Equip-over-ghost channel: magic round-trip (increment 1) and real loadout
  render, both confirmed in-game (2026-06-17/18); the vtable-slot-patch hook
  mechanism (server slot 54, client slot 56).
- charStats→`CmCharacterInfo`→`CmPlayerEquipment` resolution and the per-NPC bind
  (`lifxBindNpc`), confirmed live on `_d` charID 1 (2026-06-16/17).
- `NetObject::setScopeAlways 0x54AC70` GID acquisition + `Sending new ghost
  NPCDecorative classId=101` (2026-06-17).
- Base body renders on a bound, ghosted NPCDecorative (2026-06-17).

**Reverse-engineered (static / disasm, not all runtime-exercised):** the
charID-getter sentinel (`NPCDecorative 0x2E3390 = mov eax,0xFFFFFFFE`), the
`0x0A11C0` resolution logic, `setCharacterBacked 0x95D40`, the offline cci
construction path, the get-or-create key (`0x28CB20`, key not yet pinned), and
the client equip-render fields `[body+0x118]`/`[body+0xe0]`/`[body+0x100]`
(inferred from ShapeBase layout, unverified on a live NPCDecorative client
instance). Held-weapon mount and the `EquipmentEvent` numeric opcode are
uncertain.

**Open gaps:** combat (take damage / killable / actually drop worn loot), the
two loadout-map bugs (raw-ptr key, non-persistent scope), and folding the patrol
tree + datablock `behavior` into the versioned mod.

**RVA cross-check.** Every RVA above that maps to a named `CmOffset` was verified
against `source/server/cm_offsets.h`. Two notes flagged in-body: (1) the slot-44/48
vs 249/253/252/256 numbering for `spawnLootstone`/`onDeathHappens` (RVAs identical
either way); (2) `_setSlotDB 0x1EEA40` (`EQUIP_SET_SLOT_DB`, SQL persist) vs the
distinct lower-level `_setSlot 0x1EE930` (itemId write) — older notes conflated
the names. No RVA in `cm_offsets.h` contradicts a value here.
