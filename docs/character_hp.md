---
title: Character HP
status: re
domain: reverse-engineering
tags: [character-hp, damage, hooks]
related: [character_ai_re.md, effects_and_abilities.md]
updated: 2026-06-26
---
# Character HP — solved

LiFx can read and write a connected character's hard HP from the server console. The investigation took multiple sessions; this doc is the post-mortem + reference for the resulting commands and the engine internals they use.

## TL;DR

```
Lifx::setHardHp(<charID>, <hp>);          # set hard HP to exact target
Lifx::applyHpDamage(<hard>, <soft>);      # apply damage (positive) or heal (negative)
Lifx::dumpCharStats();                    # list all online charIDs + their live HP
Lifx::kill(<charID>);                     # drop HP to 0 AND trigger the lethal vtable
Lifx::stageHitOverride(<hard>, <soft>);   # next real combat hit will get these values
```

All commands run against the **charID → charStats** registry that `Process_tick` populates automatically — no "take a hit first" requirement. The target just needs to be connected.

## How HP actually works in LiF

The path a real combat hit takes, end-to-end:

```
            ┌────────────────────────────────────────┐
            │ Player::_applyHit  (RVA 0xEE0F0)       │  the orchestrator
            │   1. Calc_hit_damage_distribution       │
            │   2. ONEPUNCHMAN (damage calc)          │  fills dmgPacket{hardHp, softHp, …}
            │   3. CmCharacterWounds::dealDamage      │  applies wound state
            │   4. FUN_140090F60(charStats, &dmgPkt) │  ← THE HP-WRITE STEP
            │   5. CmCharacterInfo::_sendChanges     │  broadcasts stat events (no HP)
            │   6. lethal-check + Player::vtable[38]  │  death/unconscious trigger
            └────────────────────────────────────────┘
                              │
                              ▼  step 4 in isolation
            ┌────────────────────────────────────────┐
            │ apply_damage(charStats, pkt)            │
            │   reads pkt[0..1] = hardHp / softHp dmg│
            │   subtracts from live charStats fields  │
            │   triggers the network sync that         │
            │   updates the client HUD                 │
            └────────────────────────────────────────┘
```

`Lifx::applyHpDamage` calls **just step 4** directly. That gives a silent, no-orchestration HP adjustment perfect for admin tooling — no chat spam, no sound, no wound side-effects, no animation.

`Lifx::kill` runs step 4 with overkill damage AND directly invokes the lethal vtable from step 6.

### Live HP field layout (on `charStats`)

`charStats` is a sub-object inside the Player class at offset `+0xAA8`. Its layout (verified by diffing pre/post `applyHpDamage(5, 0)`):

```
charStats + 0x118  = -hardHp × 1e6  (int64)     ← THE live hard HP field
charStats + 0x109C = charID (uint32)             ← back-pointer to Player+0x1B44
```

So display HP = `-(field at +0x118) / 1e6`. Damage is positive in the packet ⇒ field becomes less negative ⇒ display HP drops.

### Soft HP, stamina, food, thirst

All live in the same `charStats` struct. Offsets known from the wide scan:

| Field | Offset | Notes |
|---|---|---|
| Hard HP raw | `+0x118` | confirmed. **HUD hard HP = `floor(-field/1e6) + 1`** (1-based "minimum alive = 1" display), capped at `+0x110` max. `setHardHp` precise across all states. |
| Hard HP max | `+0x110` | nominal max HP (Constitution-driven). The HUD doesn't scale by this — it's the cap. |
| Soft HP damage | `+0x1F8` | "damage" term of the soft-HP triplet. See *Triplet formula* below. |
| Soft HP bonus  | `+0x200` | "bonus" term — process_tick adjusts this for natural regen/wound healing. |
| Soft HP effMax | `+0x218` | "effective max" term used in the triplet sum (idle = 0). |
| Soft HP max | `+0x1F0` | nominal max soft HP |
| Stamina-shaped | `+0x200` | ~+2.5/sec idle regen |
| Food level | `+0x2D8` | confirmed by HUD correlation — `-field/1e6` matches the food stat (e.g. `-92137466` → 92.137 food). Same negation convention as hard HP. Slow ~+0.026/sec drain at idle. |

Both HP fields use the same negated-`×1e6` convention: `display = -field / 1e6`. To map the rest of the stats, use the same diff technique: `Lifx::vitalMark(); <change one stat>; Lifx::vitalDiff();`.

### `Player` accumulated-damage fields (death-check inputs)

The death checks in `Player::_applyHit` (lines 602-616) read offsets *on the Player object*, not on `charStats`:

```c
// death cond A
if ((Player[0x17c] - Player[0x178]) + Player[0x179] < 1)  →  unconscious / die
// death cond B
if ((Player[0x198] - Player[0x194]) + Player[0x195] < 1)  →  alternate lethal branch
```

`Player[0x17c]` is `*(int64*)(Player + 0xBE0)` etc. (`param_1` is `longlong*`, so index ×8). `Lifx::dumpPlayerDeathFields(<charID>)` prints them and the two derived conditions.

This is why `setHardHp(<charID>, 0)` shows 0 on the HUD but the character doesn't fall over, and equally why `setSoftHp(<charID>, 0)` doesn't knock anyone out: we wrote `+0x118` / `+0x1F8` but the accumulated counters at `Player + 0xBC0` / `+0xCA0` etc. didn't move, so the natural death/knockout checks never fire. `Lifx::kill(charID)` invokes vtable slot 38 (death) directly; `Lifx::knockout(charID)` invokes vtable slot 40 (unconscious).

## The charID → charStats registry

Populated in `Hooks::VitalParams::ProcessTick` (`hook_vital_process_tick.cpp`):

```cpp
const uint32_t charID = *(uint32_t*)((char*)charStats + 0x109C);
if (charID != 0 && charID < 0x40000000u) g_registry[charID] = charStats;
```

`Process_tick` fires ~30/sec per connected character, so within ~30ms of any player connecting they're in the registry. Lookup is `Hooks::VitalParams::LookupCharStats(charID)`.

## Things that DO NOT work (and now we know why)

| Approach | Why it failed |
|---|---|
| Direct write to `CmCharacterInfo + 0x194` | Persistence shadow, only consulted at save/load. Not what the HUD reads. |
| `CmCharacterInfo::PersistHp` + `SendChanges` | Persist reloads from DB clobbering the write; SendChanges' payload is stats-only, not HP. |
| TS `Player::applyDamage` / `setDamageLevel` | Wrong fields on the wrong object (ShapeBase's `mDamageLevel`, unused by LiF for HP). |
| Hooking `dealDamage` (0x1C63E0) standalone | Did fire — but only applies wound state, doesn't write live HP or push to client. (Earlier theory about LTO-inlining was wrong; it's reachable, just insufficient.) |
| Direct writes to `+0x118` / `+0x1F8` / `+0x180/188` / `+0x260/268` on charStats | Engine's natural tick recomputes these from other state; our writes drift back. The trick is to push through `apply_damage(0x90F60)` so the engine treats the change as authoritative. |

## Critical offsets / RVAs

### Engine functions

| Name | RVA | Role |
|---|---|---|
| `Player::_applyHit` | `0xEE0F0` | The orchestrator. Identified via error string `"x:\\dev\\cm_clone\\cm_yo_release\\engine\\source\\t3d\\player.cpp" "Player::_applyHit" 0xf92`. |
| `apply_damage` (anonymous) | `0x90F60` | Step 4. Takes `(charStats, &dmgPacket)`. Writes live HP + pushes network sync. **The function we call to set HP.** |
| ONEPUNCHMAN (damage calc) | `0xA4BF0` | Step 2. Returns `{hardHpDmg, softHpDmg}` in an output struct. Hookable for damage-injection. |
| `CmCharacterWounds::dealDamage` | `0x1C63E0` | Step 3. Wound-state only. |
| `Calc_hit_damage_distribution` | `0x91A50` | Step 1. `self` is `charStats`. `extra` is `CmCharacterInfo*` (= same address as `CmCharacterWounds*` — they're the same struct). |
| `CharacterVitalParameters::Process_tick` | `0x97BC0` | Per-frame tick on `charStats`. Our hook here populates the charID registry. |
| `CmCharacterInfo::_sendChanges` | `0x1BC3D0` | Stats broadcast. **Does NOT carry HP** — empirically observed to only fire on spawn (mask bits 10-15 + full sync), never on a combat hit. |

### Object layout

```
Player                                              Player + 0x0
├── (Torque3D ShapeBase fields)
├── +0xAA8   charStats (sub-object)                 ← Process_tick.this
│             │
│             ├── +0x000   vtable
│             │             [38]  = lethal/unconscious virtual (offset 0x130 in vtable)
│             ├── +0x118   -hardHp × 1e6 (int64)    ← LIVE HARD HP
│             ├── +0x200   stamina-shaped (regen)
│             ├── +0x2D8   slow-drain stat (hunger/thirst-shaped)
│             └── +0x109C  charID (uint32, mirrors Player + 0x1B44)
│
├── +0xBC0   Player[0x178]   death-check input A
├── +0xBC8   Player[0x179]
├── +0xBE0   Player[0x17C]
├── +0xCA0   Player[0x194]   death-check input B
├── +0xCA8   Player[0x195]
├── +0xCC0   Player[0x198]
└── +0x1B44  charID (uint32)
```

## Commands

### Production commands

| Command | Description |
|---|---|
| `Lifx::setHardHp(charID, hp)` | Set hard HP to an exact target. Reads live HP, computes delta, applies via `apply_damage`. |
| `Lifx::setSoftHp(charID, hp)` | Set soft HP to an exact target. Same mechanism using `charStats + 0x1F8`. |
| `Lifx::applyHpDamage(hardHp, softHp)` | Direct apply: positive = damage, negative = heal. Display HP units. Uses last-captured `charStats` (any charID). |
| `Lifx::kill(charID)` | Drop HP to 0 + invoke the lethal vtable (`charStats->vtable[0x130/8]` = slot 38). Triggers the full engine death sequence: animation, "lost consciousness", respawn window. Confirmed end-to-end. |
| `Lifx::knockout(charID)` | Drop soft HP to 0 + invoke the unconscious vtable (`charStats->vtable[0x140/8]` = slot 40). Triggers the in-game unconscious state that normally fires when soft HP runs out. |
| `Lifx::dumpCharStats()` | Print the charID → charStats registry with each entry's live HP. |
| `Lifx::stageHitOverride(hardHp, softHp)` | Replace the damage values on the *next* real combat hit. Useful for "make the next punch deal exactly 50 damage". |

### Diagnostic commands (kept for future extension)

| Command | Description |
|---|---|
| `Lifx::vitalMark()` / `Lifx::vitalDiff()` | Snapshot + diff `+0x100..+0x400` of last-seen charStats. Used to identify new field offsets. |
| `Lifx::vitalPoke(offset, int64)` | Write a raw int64 to charStats + offset. Mostly defeated by the engine's tick recompute; use `apply_damage` instead. |
| `Lifx::dumpPlayerDeathFields(charID)` | Print Player+0xBC0/BC8/BE0/CA0/CA8/CC0 + the two death-condition expressions. |
| `Lifx::pokePlayer(charID, hexOffset, value)` | Direct write to Player+offset. Use to experiment with driving the natural death-checks below 1. |
| `Lifx::woundDump()` / `Lifx::dealDamage(bodyPart)` | The wound-state primitive. Doesn't move HP on its own but is the engine's per-body-part damage trigger. |
| `Lifx::getPlayerHp(charID)` / `Lifx::setPlayerHp(charID, hp)` / `Lifx::setPlayerHpMemoryOnly` / `Lifx::setPlayerHpPersistOnly` | Operates on the **persistence shadow** (`CmCharacterInfo + 0x194`). Doesn't move the HUD. Useful for testing the divergence between live and persisted HP. |

## Precision

**Hard HP**: precise. `setHardHp(charID, N)` lands HUD on exactly N for any N in [0, max]. Formula: write `+0x118 = -(N - 1) × 1e6`. Verified across full range of test targets (1, 50, 75, 100, max).

**Soft HP**: precise. `setSoftHp(charID, N)` lands HUD on exactly N. Formula (same shape as hard HP): write `+0x1F8 = -(N-1)×1e6`, `+0x200 = 0`, `+0x218 = 0`, then call `apply_damage` with `pkt[1]=1` (negligible 1e-6 HP) to force the `send_changes` broadcast. The HUD then reads `effective = (+0x218 - +0x1F8 + +0x200) + 1` = N exactly.

### Triplet formula

The death-gate in `Process_tick` (RVA `0x97BC0`) reads:

```c
if ((cs[0x27] - cs[0x23]) + cs[0x24] < 1)  vtable[0xa8](cs);   // death virtual
```

So **effective HP is a three-term sum**, not a single field:

- Hard: `(+0x138 - +0x118 + +0x120)` — see `Engine::HardHpEffective`
- Soft: `(+0x218 - +0x1F8 + +0x200)` — see `Engine::SoftHpEffective`

HUD displays `effective/1e6 + 1` (1-based "alive ≥ 1"), capped at the nominal max. The earlier "HUD = `floor(-field/1e6) + 1`" shortcut worked for hard HP only because `+0x138` and `+0x120` happened to stay near zero in our tests; the *real* formula is the triplet. Pure `+0x1F8` writes don't move the HUD because process_tick keeps `bonus` rebalancing — and apply_damage's heal path shifts `bonus` independently of our intent, which was the source of the ±10 HP drift we chased through PR #5 and #7.

## How the Detours hooks are kept alive

`Server::AttachHooks` and `Server::DetachHooks` (in `cm_server.cpp`) are wrapped in explicit `DetourTransactionBegin/Commit` because the call sites run *after* DllMain's transaction has committed. The post-commit verification reads the first byte of each hooked function and echoes `(patched)` if it's `0xE9`. **Don't remove that verification** — silent-attach-failure was the bug that wasted multiple sessions earlier.

See `feedback_detours_transaction.md` in the auto-memory store for the full lesson.

## File pointers

- LiFx command surface: `source/server/api/lifx_character.{h,cpp}`
- Engine offsets / inline helpers: `source/server/hooks/furnace/engine_internals.h`
- charID registry + Process_tick hook: `source/server/hooks/character/hook_vital_process_tick.{h,cpp}`
- The HP-write hook: `source/server/hooks/character/hook_apply_damage.{h,cpp}`
- Detours transaction wrapper + verification: `source/server/cm_server.cpp::AttachHooks`
- Offset table: `source/server/cm_offsets.h`
- Engine decompiles used during the investigation: `/tmp/lifx_ghidra/decompile/dd_*.c`

## Extending: what's open

- **Make `setHardHp(<charID>, 0)` actually kill** without a separate `kill` call — write the death-check fields (`Player+0xBC0/BE0/...`) at the same time as the HP field so the natural lethal branch fires on the next combat event. `Lifx::dumpPlayerDeathFields` is the tool for this exploration. (Workaround already exists: `Lifx::kill(charID)` triggers full death sequence directly.)
- **Per-player permission / safety** — `kill` is currently a raw admin verb. Future work: scope it to admin charIDs, add a confirmation token.
- **Wire ONEPUNCHMAN hook** (`hook_onepunchman.{h,cpp}` exists in tree, not yet built) — gives us per-hit damage-value access without going through `Player::_applyHit`. Useful if we want fine-grained combat tweaks (e.g. damage scaling, friendly fire toggle).
