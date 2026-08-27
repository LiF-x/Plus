---
title: Bandit held-weapon render, native melee strike & stamina-paced AI swing
status: re
domain: reverse-engineering
tags: [hostile-npc, bandit, combat, melee, weapon, stamina, animation, hooks, charstats, ai]
related: [animal_spawn.md, npc_class_hierarchy.md, hostile-npc-ai-path-comparison.md, character_ai_re.md, character_hp.md, animal_data_model.md]
sources: [source/server/cm_offsets.h, source/server/api/lifx_hostile.cpp, source/server/hooks/character/hook_setanimation.cpp, source/server/hooks/character/hook_animal_create.cpp]
updated: 2026-06-28
---

# Bandit held-weapon render, native melee strike & stamina-paced AI swing

The #154 hostile bandit is a Wolf-type `Animals::Animal` (`animalTypeId = 755`) rendered with
`male.dts` and bound to a real character (see [`animal_spawn.md`](animal_spawn.md)). This page
covers making it **fight like an armed humanoid**: it visibly **holds a weapon**, its AI swings
**deal real damage** through the engine's own melee path, and the swing rate is **paced by a
stamina pool** so it is not a frame-perfect blender. Everything here is **cci-free** (no
`CmCharacterInfo` construction) — the same low-risk class as the render path.

> **For the behaviour-tree work:** the native AI attack node already fires the swing animation
> on its own; the bandit's *damage* and *pacing* are added in the **`setAnimation` Detour**
> (`Hooks::AnimRemap::OnSetAnimation`), not in the tree. See [§ AI-driven swing](#ai-driven-swing--why-the-swing-used-to-restart) — if you change how/when the tree drives `Attack_Fast`/`Attack_Power`, that hook is where the hit + stamina gate live.

## TL;DR mechanism

| Concern | Native fact | What LiFx does |
|---|---|---|
| Hold a weapon | `Player::Mount_movable_object` mounts a `ShapeBaseImageData` into image slot 6 | Auto-mount image id **556** once per bandit at spawn (`MountWeaponOnce`) |
| Deal damage | `Animals::Animal::endAttack` is the real cone hit-scan + damage apply; the AI swing only plays an animation | Call `endAttack` once per committed swing (`FireBanditHit`) |
| Why no native damage | `endAttack` is normally fired by the per-tick gate at `0x18BD30` on the **DTS attack-sequence trigger marker**, which the `male.dts` remap dropped | Invoke `endAttack` directly from the swing hook |
| Swing restarts | AI alternates `Attack_Fast`/`Attack_Power` each tick → index changes → engine anti-restart guard misses | Gate swings on a stamina pool; surplus re-issues are dropped so the swing plays out |
| Swing too fast | The AI has no stamina to pace it | Per-bandit stamina pool, cost scaled by the real weapon, regen over time |

## Held-weapon render

`Player::Mount_movable_object` (`MOUNT_MOVABLE_OBJECT = 0xEBA30`) renders a held weapon with no
cci. It is called with `param_1 = animal + 0xAA8` (the charStats subobject; internally it derefs
`param_1 - 0xAA8` back to the `Animal` `this`), resolves a `ShapeBaseImageData` by **movable
TypeID** via the chained-hash resolver `FUN_140120b80`, then mounts it through `obj->mountImage`
(vtable **+0x3C0**) into **image slot 6**. A bad id logs `Can't find ShapeBaseImageData for
movable TypeID=%u` and returns — safe to probe.

- The valid held-weapon id is **`556`** (the item's `ObjectTypeID`), confirmed in-game.
- The id space is keyed by **`ShapeBaseImageData` datablock id**, *not* `WeaponData` id. Enumerate
  it live with `Lifx::dumpMovableImages [filter]` (walks the bucket array at
  `MOVABLE_IMG_BUCKETS_PTR = 0xB6FB10`, count at `MOVABLE_IMG_BUCKET_COUNT = 0xB6FB18`; node =
  `[0]=next, +0x8=key(id), +0x10=value(image*)`).
- **Auto-mounted at spawn:** `MountWeaponOnce` (in the `setAnimation` hook) mounts id 556 the
  first time a bandit animates (the shape instance is live by then), guarded once per animal. The
  mounted image replicates to clients as ShapeBase image state, so every observer sees the weapon.
- Manual probe: `Lifx::mountBanditWeapon (movableTypeId)`.

## Native melee strike — `Animals::Animal::endAttack`

`endAttack` (`ANIMAL_END_ATTACK = 0x18A4D0`) is the **real animal melee resolution** and is fully
self-contained:

1. Reads the animal's **`WeaponData*`** at `this+0x24f0` (`FUN_1400bde00` → index; **-1 ⇒** logs
   `animal %s without weapon tries to attack` and bails). This pointer is populated **at spawn** by
   `Animals::Animal::onNewDataBlock` (`0x18B2C0`) from the AnimalData datablock's weapon name field
   (`datablock+0x8488`) — so a Wolf-datablock bandit already has its bite `WeaponData` here.
2. Reads attack params from the creature datablock `*(this+0x2530)` indexed by the attack-type
   `*(this+0x24f8)` (`0`=fast, `1`=power): range `@+0x84c0`, cone half-angle `@+0x84c4`, damage
   min/max `@+0x84c8/+0x84cc` (per-type stride `0x14`).
3. Spatial-queries nearby objects, RTTI-casts each to **`Player`**, filters by range + cone.
4. Builds a hit descriptor (`FUN_1400a41d0`, category 5) using the weapon's `Hit_group_type`
   (`0xBDD30`) and `Hit_group_damage_level` (`0xBDCE0`, reads `weapon+0x2390+type*4`), then applies
   it: **`FUN_14051f030(victimPlayer, descriptor, attackerAnimal)`** → the victim's virtual slot
   **`+0x350`** → spawns a `ServerCombatHitEvent`. No cci anywhere on this path.

**Why the bandit dealt no damage before:** the AI swing (`ANIMAL_SWING = 0x18B950`) only builds the
string `"Attack_Fast"`/`"Attack_Power"` and calls `setAnimation` — **no damage logic**. Natively,
`endAttack` is fired by the per-tick gate `0x18BD30` when the wolf attack **DTS sequence trigger
marker** hits. The `male.dts` animation remap dropped that marker, so `endAttack` never ran. We
therefore call it explicitly.

`FireBanditHit(self)` (in the swing hook) null-checks the weapon pointer, clamps the attack-type to
`0/1`, clears the per-swing consumed flag `@+0x24fc`, then calls `endAttack(self)` once per swing.
Manual probe: `Lifx::banditStrike ([attackType] [visualSwing])`.

**Runtime-verified:** drives the full pipeline to real wounds —
`CmCharacterWounds::dealDamage(Torso)` / `injure(Torso, Wound, 5400000)` / `… Bleeding`, hitting
real body parts (Torso, Left leg) on a nearby player.

## AI-driven swing & why the swing used to restart

The native AI attack node (`AI::Nodes::Attack::process = 0x193400`) calls the swing once then polls
`FUN_1402e2740` for completion. The swing sets a thread index via `SetAnimByIndex`
(`0x2E2520`), which **already has an anti-restart guard** — but it only matches when the *same*
index is re-requested:

```c
// SetAnimByIndex(self, idx, flag)
if (self[0x49c] == idx && pos(self) < threshold) return;   // same anim still playing -> skip
... (**(self + 0x1a0))(self, 0x800000000000);              // setMaskBits -> thread IS networked
```

The wolf AI **alternates `Attack_Fast` (idx 204) ↔ `Attack_Power` (idx 326)** every server tick
(~0.5s). Because the index keeps changing, the guard never trips and the male swing was restarted
at frame 0 every tick — visibly cut short. (Idle/walk replicate fine, confirming general thread
replication works; only the alternating one-shot attack was affected.)

The fix lives in `OnSetAnimation`: for a hostile requesting an `Attack_*` animation we gate on a
**stamina pool** (below). While the bandit is mid-swing / winded, the surplus re-issues are
**dropped**, so the in-flight swing plays to its end; on a committed swing we set the animation
**and** call `FireBanditHit`. The hit is thus part of the AI's natural attack, not a console
command.

## Stamina pacing

A player's swing rate is limited by **stamina** (each swing costs it, you recover when winded); the
AI has no such governor. We model it with a **per-bandit pool** (`g_stam`, keyed by the animal
pointer), calibrated to player-observable values:

| Tunable | Value | Meaning |
|---|---|---|
| `kStaminaMax` | `100` | player soft-stamina bar capacity |
| `kRegenPerSec` | `12` | ~full refill in ~8s |
| `kBaseSwingCost` | `12` | ~8 swings to empty at the reference weapon |
| per-swing cost | `kBaseSwingCost × (weaponDmgLevel / kRefWeaponDmg)`, clamped `[8, 24]` | scaled by the **real** equipped weapon's `Hit_group_damage_level[0]` (`WeaponData+0x2390`) |
| `kMinSwingMs` | `1000` | animation-completion floor between swings |

Each `Attack_*` request regenerates the pool by `regen·dt`, then commits a swing only if the
animation floor has passed **and** stamina ≥ the weapon's cost (deducting it). Otherwise the bandit
is winded and skips — producing the swing → drain → recover rhythm.

### Why a simulated pool, not the engine's real soft-stamina

The player's combat stamina is **soft-stamina**, a **1e6-scaled multi-field fixed-point "vital
pool"** inside `CharacterVitalParameters` (current = `(p[0x51] − p[0x4d]) + p[0x4e]` across the
`0x4b–0x53` cluster; `On_soft_stamina_exhausted = 0x96D00` sets the zero-flag at `vitalparams+0x435`
and the Fatigued state). It is reached only through **virtual accessors** in a **stripped** binary,
and its regen vital-tick (base slot `VITALPARAMS_PROCESS_TICK = 0x97BC0`, never-called abstract;
concrete override unidentified) **does not run on an NPC**. Draining the real stat would therefore
risk the bandit getting **stuck winded**. The pool reproduces the player's stamina economy safely;
the per-swing **cost is still sourced from the real weapon** (its damage-level). The `[Stamina]`/
`staminaRegen` datablock fields found by string search are the **horse mount** system, not character
combat stamina.

## Key RVAs & offsets (image base `0x140000000`, server)

| Symbol | RVA | Note |
|---|---|---|
| `Player::Mount_movable_object` | `0x0EBA30` | held-weapon render; arg1 = `animal+0xAA8`, arg2 = movable id |
| movable-image resolver | `0x120B80` | hash by ShapeBaseImageData datablock id |
| `Animals::Animal::endAttack` | `0x18A4D0` | native cone hit-scan + damage apply |
| AI swing (animation only) | `0x18B950` | builds `"Attack_Fast"`/`"Attack_Power"`, `setAnimation` |
| per-tick contact gate | `0x18BD30` | native trigger that calls `endAttack` (dead for `male.dts`) |
| `onNewDataBlock` (sets weapon) | `0x18B2C0` | writes `WeaponData*` to `this+0x24f0` |
| `NPCS::AnimatedNPC::setAnimation` | `0x2E2A90` | hooked (`ANIMATED_NPC_SET_ANIMATION`) |
| `SetAnimByIndex` | `0x2E2520` | thread set + anti-restart guard + `setMaskBits` |
| swing-completion poll | `0x2E2740` | AI uses to know a swing finished |
| hit-descriptor builder | `0x0A41D0` | category-5 melee descriptor |
| apply-hit to victim | `0x51F030` | `victim->vtbl[+0x350]` → `ServerCombatHitEvent` |
| `On_soft_stamina_exhausted` | `0x096D00` | real soft-stamina exhaustion (left untouched) |

**Animal `this` field offsets:** `+0x24E0` current sequence index · `+0x24F0` `WeaponData*` ·
`+0x24F8` attack-type (0/1) · `+0x24FC` per-swing hit-consumed byte · `+0x2530` creature datablock
· `+0xAA8` charStats delta (`::Engine::kCharStatsToPlayerDelta`).
**`WeaponData` offsets:** `+0x2390` `Hit_group_damage_level[0]` (float) · `+0x23A4` `Hit_group_type`.

## Console commands

| Command | Purpose |
|---|---|
| `Lifx::banditStrike ([attackType=0] [visualSwing=1])` | manual native swing + `endAttack` hit on the last bandit |
| `Lifx::mountBanditWeapon (movableTypeId)` | manual held-weapon mount probe (556 = sword) |
| `Lifx::dumpMovableImages ([filter])` | enumerate registered movable-image ids |

## Status & provenance

- **Runtime-verified:** held-weapon render (id 556, in hand); native `endAttack` deals real wounds
  to a player (Torso / Left leg, with Bleeding); auto-mount at spawn; AI-driven swing fires the hit
  without a console command; stamina pool paces the swing rate.
- **Inferred / by design:** the stamina **pool** is a calibrated simulation (capacity + regen are
  player-observable values, cost is scaled from the real weapon damage-level); it is **not** the
  engine's real soft-stamina stat (left untouched for safety, see above). The weapon-cost scaling
  reference `kRefWeaponDmg` is provisional — a one-time `[lifx-stam]` log prints the live
  `weaponDmgLevel`/`cost` so it can be calibrated.
- **Open follow-ups:** confirm the swing animation now plays fully on the client under the stamina
  floor; (optional) full-fidelity native soft-stamina drain if the NPC regen path is ever wired.

See [`hostile-npc-ai-path-comparison.md`](hostile-npc-ai-path-comparison.md) for the Animal-vs-
NPCDecorative platform choice and [`npc_class_hierarchy.md`](npc_class_hierarchy.md) for why melee
combat nodes gate on `Animals::Animal`.
