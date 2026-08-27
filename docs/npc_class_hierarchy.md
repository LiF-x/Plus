---
title: NPC / Animal class hierarchy
status: verified
domain: reverse-engineering
tags: [rtti, npc, animal, ai, vtable, class-hierarchy]
related: [ai_and_spawning.md, hostile-npc-ai-path-comparison.md, character_hp.md, reverse_engineering.md]
sources: [source/server/cm_offsets.h, source/server/hooks/ai/hook_behavior_node.cpp, source/server/hooks/ai/ABI_NOTES.md, source/server/hooks/ai/VERIFICATION.md, hostile-npc-ai-path-comparison.md, ai_and_spawning.md]
updated: 2026-06-26
---

# NPC / Animal class hierarchy

## TL;DR

The LiF server's AI-capable creatures all descend from `Player`. RTTI base-class arrays
extracted from `ddctd_cm_yo_server.exe` (image base `0x140000000`) give one tree with two
sibling branches under `NPCS::Base`: `AnimatedNPC → Animals::Animal`, and
`PlayerBased → NPCDecorative`. **Every** node in this tree therefore carries *both* the
Player character layout (charStats / equip / `CharacterCriminals` / `IDamagableCreature`) *and*
the AI layout (behaviour-tree ptr `+0x24B8`, move-engine ptr `+0x24C0`) — the AI machinery is
written by the common `NPCS::Base` ctor, so there is no "AI half vs character half" divide. The
practical divergence is purely structural: the native combat / perception behaviour nodes are
hard-gated to `Animals::Animal` (blackboard key `"animal"`) and touch Animal-only offsets, so they
are **not** reusable on `NPCDecorative` without re-platforming. `NPCDecorative` is what the shipped
hostile/vendor NPC work spawns; the chosen path keeps it and reimplements perception as gate-less
custom `"npcbase"`-keyed nodes.

## The hierarchy

```
Player                       ctor 0x0E87B0
 │   (carries CharacterParameters / CharacterCriminals / IDamagableCreature
 │    = the charStats @ +0xAA8, equip, criminal-tag, damageable layout)
 │
 └─ NPCS::Base               ctor 0x2E2C10   vtbl RVA 0x7E28D8
     │   writes BOTH charStats (+0xAA8) AND the AI layout:
     │     +0x24B8 = behaviour-tree ptr      (NPC_AI_TREE_OFF)
     │     +0x24C0 = move-engine ptr         (NPC_MOVE_ENGINE_OFF)
     │     +0x24CC / +0x24D0 = range floats 50.0 / 100.0
     │
     ├─ NPCS::AnimatedNPC    ctor 0x2E2390   vtbl RVA 0x7E1C98   obj size 0x24F0
     │   │   adds only: +0x24E0 = -1, +0x24E8 = 0
     │   │
     │   └─ Animals::Animal                  vtbl RVA 0x798AA0
     │         adds the perception block:
     │           +0x2500 = enemy list
     │           +0x2520 = current target
     │           +0x2528 = threat value
     │           +0x2540 = manager animal id (ANIMAL_MGR_ID_OFF)
     │
     └─ NPCS::PlayerBased    ctor 0x2E5210   vtbl RVA 0x7E5638
           │
           └─ NPCS::NPCDecorative  ctor 0x2E46B0  vtbl RVA 0x7E3D08  obj size 0x2518
                 ← what the shipped hostile/vendor NPC work spawns
```

Full virtual addresses for the vtables (image base `0x140000000`): `Base` `0x1407E28D8`,
`AnimatedNPC` `0x1407E1C98`, `NPCDecorative` `0x1407E3D08`.

### Named offsets / RVAs that map to `cm_offsets.h`

| Symbol in `cm_offsets.h` | Value | Meaning |
|---|---|---|
| `NPCS_BASE_CTOR` | `0x2E2C10` | `void __fastcall(self)` — writes charStats `+0xAA8`, tree `+0x24B8`, engine `+0x24C0` |
| `NPCDECORATIVE_CTOR` | `0x2E46B0` | `void __fastcall(self)` |
| `NPCDECORATIVE_CREATE` | `0x2E4AB0` | `NPCDecorative* __fastcall()` — `malloc(0x2518)` + ctor + `registerObject` (vtbl `+0x50` = `0x4304A0`) |
| `NPCDEC_VTABLE` | `0x7E3D08` | `NPCDecorative` primary vtable (RVA); equals VA `0x1407E3D08` |
| `NPC_AI_TREE_OFF` | `0x24B8` | creature `+0x24B8` = behaviour-tree ptr |
| `NPC_MOVE_ENGINE_OFF` | `0x24C0` | creature `+0x24C0` = move-engine ptr |
| `ANIMAL_VTABLE` | `0x798AA0` | `Animals::Animal` primary vtable (observed live) |
| `ANIMAL_MGR_ID_OFF` | `0x2540` | animal `+0x2540` = manager animal id |
| `SIMOBJECT_ID_OFF` | `0x90` | `SimObject` `mId` (u32), assigned by `registerObject` `0x4304A0` |

`Player` ctor `0x0E87B0`, `PlayerBased` ctor `0x2E5210` / vtbl `0x7E5638`, `AnimatedNPC` ctor
`0x2E2390`, and `AnimatedNPC` vtbl `0x7E1C98` / `Base` vtbl `0x7E28D8` are **not** named
constants in `cm_offsets.h`. The `Player` / `PlayerBased` ctors and the `PlayerBased` vtbl appear
as raw hex in `cm_offsets.h`'s A2a comment block (lines 365–366); the `AnimatedNPC` ctor is in
`docs/hostile-npc-ai-path-comparison.md`; the `AnimatedNPC` / `Base` vtable RVAs come from the RTTI
base-class-array extraction (see *Status & provenance*). The `+0xAA8` charStats offset is corroborated by
`cm_offsets.h` (`NPCS_BASE_CTOR` comment, `SHAPEBASE_PACKUPDATE` comment) and
`source/server/hooks/ai/hook_behavior_node.cpp:722` ("npcbase creature's Player charStats
`creature+0xAA8`").

## Why "spec the AnimatedNPC interface onto NPCDecorative" has nothing to port

Both primary vtables are **196 slots**. Diffing them against the common `NPCS::Base` vtable:

- `AnimatedNPC` overrides 11 `Base` slots: `{0, 27, 28, 54, 55, 108, 178, 179, 180, 183, 194}`.
- `NPCDecorative` overrides a **strict superset** — those same 11 slots **plus 7 more**.

The "AnimatedNPC-only" override set is therefore **empty**. AnimatedNPC adds no interface that
NPCDecorative lacks. The AI-tree / move-engine / range machinery is set by the **common
`NPCS::Base` ctor** (`0x2E2C10`), so `NPCDecorative` already has it — which is exactly why the
shipped custom `GoToPoint` / patrol nodes already drive it (see `ai_and_spawning.md`,
`hostile-npc-ai-path-comparison.md`).

This also corrects the earlier A2a framing that "the character half and the AI half don't combine
cheaply": **wrong at the type level.** `AnimatedNPC` and `Animals::Animal` and `NPCDecorative` are
*all* `Player`-derived, so each structurally carries the charStats / equip / `CharacterCriminals` /
`IDamagableCreature` half *and* the `NPCS::Base` AI half. The real divergence is the sibling
branch, not "AI vs character."

## The combat / perception nodes gate on `Animals::Animal`, not AnimatedNPC

The native target-acquisition and threat nodes resolve their subject through the AI blackboard via
the class key **`"animal"`** (string at VA `0x14073B128`), using the
`SimObjectPtr<Animals::Animal>` blackboard-find at `0x190D70`. On a non-`Animal` creature that
resolve returns null, and the node logs `object is nullptr` — the in-game crash
`AI::Nodes::EnemyInRange::process() - object is nullptr`.

| Node | RVA | Key | Touches |
|---|---|---|---|
| `EnemyInRange::process` | `0x1941A0` | `"animal"` (find `0x190D70`) | enemy-list `+0x2500`, target `+0x2520` |
| `HasTarget` | `0x194830` | `"animal"` | current-target `+0x2520` |
| `SetPlayerAsTarget` | `0x18FAB0` | `"animal"` | current-target `+0x2520` |
| `ResetTarget` | `0x18FE60` | `"animal"` | current-target `+0x2520` |
| `ThreatValueChange` | `0x190350` | `"animal"` | threat `+0x2528` |
| `AimAtClosestEnemy` | `0x193DA0` | `"npcbase"` (find `0x190E90`) | orient only — acquires **nothing** |

These nodes read/write **Animal-only offsets**: enemy-list `+0x2500`, current-target `+0x2520`,
threat `+0x2528`. There is **no `NPCS::Base`-level target slot**, and **no engine perception pass**
that populates a target for a non-Animal — acquisition is done *by the Animal-keyed nodes
themselves*. The per-class update that ticks the tree and drives the move-engine is
`Animals::Animal::packUpdate` at `0x18B450` (identified in `cm_offsets.h` `AI_TREE_PROCESS`
comment, lines 379–384); it only ticks the tree + drives the engine — it does **not** acquire
targets.

`AimAtClosestEnemy` (`0x193DA0`) is the lone exception: it is keyed `"npcbase"` (blackboard-find
`0x190E90`, returns a `SimObjectPtr<NPCS::Base>`) and so runs on **any** `NPCS::Base`, but it only
orients (calls the orient virtual `[npcbase_vtbl+0x618]`) and acquires no target.

The two blackboard-find helpers are confirmed in shipped code
(`source/server/hooks/ai/hook_behavior_node.cpp:225–231`): `"animal"` → find `0x190D70`,
`"npcbase"` → find `0x190E90`. The shipped `GoToPoint` resolves a creature by trying `"animal"`
then `"npcbase"`, which is why it works on both animals and human NPCs; the native
`GoToPosition::process` is hard-wired to `"npcbase"` and fails on animals (`npc is nullptr`).

## Consequence for hostile / vendor human NPCs

To reuse the native ~27-node animal combat suite, the object must **be** an `Animals::Animal`
(most-derived) — `AnimatedNPC` alone is insufficient, because the nodes read Animal-only offsets
behind the `"animal"` key. The fork:

- **Path 1 — re-platform onto an `Animals::Animal` derivative** (humanoid datablock): the 27-node
  animal combat suite works natively, but this abandons the shipped `NPCDecorative` equip / loot /
  persistence work and must re-verify humanoid render / anim / equip / death-loot on an `Animal`
  plus the Animal spawn lifecycle (`Animals::Manager::createAnimal` `0x195FD0`; see
  `ai_and_spawning.md`).
- **Path 2 (recommended) — keep `NPCDecorative`, write gate-less custom LiFx nodes keyed
  `"npcbase"`** (find `0x190E90`): scan-players-in-radius → set target (own side-table slot),
  in-range, face, flee, chase — reusing the **same** `NPCS::Base` move-engine (`+0x24C0`) the
  shipped `GoToPoint` already drives. Only the few perception / target nodes are reimplemented;
  equip / loot / criminal-tag all stay intact (`CharacterCriminals` is in the `Player` base chain,
  so criminal-tag flee is reachable). This extends the proven `GoToPoint` custom-node pattern.

See `docs/hostile-npc-ai-path-comparison.md` for the full decision write-up.

## Status & provenance

**Runtime-verified / directly in code:**

- The class chain `Player → NPCS::Base → {AnimatedNPC → Animals::Animal}` and
  `Player → NPCS::Base → PlayerBased → NPCDecorative` — from RTTI base-class-array extraction of
  `ddctd_cm_yo_server.exe` (2026-06-21).
- `NPCDecorative` ctor `0x2E46B0`, create `0x2E4AB0`, vtbl `0x7E3D08`, object size `0x2518`,
  `NPCS::Base` ctor `0x2E2C10`, AI-tree `+0x24B8`, move-engine `+0x24C0` — named in
  `cm_offsets.h` and exercised by the shipped hooks.
- Blackboard-find `0x190D70` (`"animal"`) and `0x190E90` (`"npcbase"`), and the `creature+0xAA8`
  charStats embedding — used in production code (`hook_behavior_node.cpp`).
- That the shipped `GoToPoint` / patrol nodes drive the `NPCS::Base` move-engine on
  `NPCDecorative` — in-game-verified (PR #120 / #122 / #124 / #126).

**Reverse-engineered (static disassembly, not all individually runtime-verified):**

- vtable slot counts (196) and the exact override-slot sets
  (`AnimatedNPC` `{0,27,28,54,55,108,178,179,180,183,194}`; `NPCDecorative` = that set + 7);
  the "AnimatedNPC-only override set is empty" conclusion.
- The combat-node RVAs (`0x1941A0`, `0x194830`, `0x18FAB0`, `0x18FE60`, `0x190350`, `0x193DA0`),
  their `"animal"` keying, and the Animal-only offsets `+0x2500` / `+0x2520` / `+0x2528`. These
  RVAs are **not** named constants in `cm_offsets.h`; they are sourced from the A2a RE pass and
  corroborated by `source/server/hooks/ai/`.
- `AnimatedNPC` ctor `0x2E2390` / fields `+0x24E0=-1`, `+0x24E8=0` / obj size `0x24F0`, the range
  floats `+0x24CC`/`+0x24D0` = `50.0`/`100.0`, and the vtable VAs `0x1407E28D8` / `0x1407E1C98` —
  static-only.

No conflicts were found against `cm_offsets.h`: every value present there
(`NPCDECORATIVE_CTOR`, `NPCDEC_VTABLE`, `NPCS_BASE_CTOR`, `NPC_AI_TREE_OFF`, `NPC_MOVE_ENGINE_OFF`,
`ANIMAL_VTABLE`, `ANIMAL_MGR_ID_OFF`, `SIMOBJECT_ID_OFF`) matches the figures above.
