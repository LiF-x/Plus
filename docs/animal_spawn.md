---
title: On-demand animal spawning
status: re
domain: reverse-engineering
tags: [animals, spawning, hostile-npc, tombstone, hooks, charstats]
related: [ai_and_spawning.md, hostile-npc-ai-path-comparison.md, character_hp.md]
sources: [source/server/cm_offsets.h, source/server/api/lifx_hostile.cpp, source/server/hooks/character/hook_animal_create.cpp, source/server/hooks/character/hook_animal_death.cpp, source/server/hooks/character/hook_setanimation.cpp, source/server/hooks_engine.cpp, ai_and_spawning.md, hostile-npc-ai-path-comparison.md]
updated: 2026-06-26
---

# On-demand animal spawning

`Animals::Animal` instances are **manager-only** — there is no console-instantiable class, so `spawnObject("Wolf", ...)` fails. The single real factory is `Animals::Manager::createAnimal` (`CmOffset::CREATE_ANIMAL` = `0x195FD0`), keyed by `animalTypeId` (Wolf = `755`). It builds the object, resolves the datablock, registers it, and adds it to the server scene so the animal **ghosts to clients natively** — no manual `setScopeAlways`. The right architecture for the #145 Animal-derived hostile is **let the engine spawn (full navmesh / collision / activation integration) and add behaviour via LiFx hooks**, not hand-roll `createAnimal` + `setTransform` (that renders but never integrates → unhittable / immobile). Hooking `createAnimal` lets us tag every spawn however it originated.

## Why `spawnObject` does not work

`spawnObject("Wolf", ...)` errors `Unable to instantiate non-conobject class Wolf`. Animals have no console class; they exist only inside the manager. The recipe in [`ai_and_spawning.md`](ai_and_spawning.md) (§ spawn-in-zone, around the `spawnObject("Wolf", ...)` example) is **wrong for this build**, and the Stage-0 `HostileNPCs` mod's `serverCmdSpawnBandit` (which used `spawnObject`) was never actually verified.

## The factory: `Animals::Manager::createAnimal`

`CmOffset::CREATE_ANIMAL` = `0x195FD0`. MSVC x64 `__fastcall`:

```
Animal* createAnimal(Manager* mgr, u32 animalTypeId, u32 quality, int id /*0 = auto*/, u8 flag)
```

What it does internally:

1. `AnimalData::GetDatablockByTypeID(animalTypeId)` → `AnimalData*` (asserts *"No animalData with type %d"*).
2. `malloc(0x2548)` — the Animal object size (note: `NPCDecorative` is `0x2518`).
3. ctor `0x188EF0` — writes `Animals::Animal::vftable` (`CmOffset::ANIMAL_VTABLE` = `0x798AA0`) at `obj+0`.
4. set datablock `0x133B80`.
5. `registerObject` `0x4304A0` — assigns the `SimObject` id at `obj+0x90` (`CmOffset::SIMOBJECT_ID_OFF`).
6. adds to the server scene → ghosts natively.

**Position is not a parameter** — set it via `setTransform` *after* the call, e.g. `<simId>.setTransform("x y z 0 0 1 0")`.

`Lifx::spawnHostile` calls it with `quality = 50`, `id = 0` (auto), `flag = 1`.

### Manager singleton

The `Animals::Manager` singleton (arg 1 to `createAnimal`) is a module-base-relative global pointer at `CmOffset::ANIMALS_MANAGER_GLOBAL_RVA` = `0xB80C90`:

```
mgr = *(void**)(ModuleBase() + 0xB80C90)
```

It is built inside `SpawnControl::SpawnControl` **only if the nav mesh is enabled** — so on a nav-mesh-less world the pointer is null and spawning is impossible.

### SimObjectId (console id)

`*(u32*)(obj + 0x90)` (`CmOffset::SIMOBJECT_ID_OFF`), assigned by `registerObject`. Use it as the TorqueScript handle for `<id>.setTransform(...)` and `<id>.method(...)`.

## Type → `AnimalData` resolution

`AnimalData::GetDatablockByTypeID` = `CmOffset::ANIMAL_GET_DATABLOCK_BY_TYPE` = `0x18C790`. It is an FNV-1a hash map keyed by `animalTypeId`:

- table pointer `DAT_140b81108`
- preloaded flag `DAT_140b81100`

| Type id | Resolves to | Notes |
|---|---|---|
| `755` | `BanditData` (`male.dts`) | `BanditData : WolfData` inherits `755` and **shares it**; the hash map returns the bandit, so type `755` already yields the humanoid, not the wolf. |

A **distinct** Bandit type needs a unique `animalTypeId` registered **both** here (the `GetDatablockByTypeID` hash map) **and** in the object-type tree (root `751`). A custom `animalTypeId` alone crashed the Stage-0 datablock preload — registration is the missing half, and is the open Phase-2 type-registration spike.

## Animal object layout

Object size `0x2548` (`NPCDecorative` = `0x2518`).

| Offset | Field |
|---|---|
| `+0x00` | `Animals::Animal` vftable (`0x798AA0`) |
| `+0x90` | `SimObject` id (`SIMOBJECT_ID_OFF`) |
| `+0xAA8` | `charStats` sub-object (Player-derived layout) |
| `+0x24B8` | AI behaviour-tree ptr (`CmOffset::NPC_AI_TREE_OFF`) |
| `+0x24C0` | move-engine ptr (`CmOffset::NPC_MOVE_ENGINE_OFF`) |
| `+0x2500` / `+0x2520` / `+0x2528` | perception block |
| `+0x2540` | manager animal id (`CmOffset::ANIMAL_MGR_ID_OFF`) — arg to `ANIMAL_MGR_REMOVE` |

## `AnimalData` datablock fields

Recovered from `AnimalData::initPersistFields` (`0x18C930`). Offsets are relative to the `AnimalData` datablock:

| Offset | Field |
|---|---|
| `+0x8458` | walkSpeed |
| `+0x845C` | runSpeed |
| `+0x8478` | `rawCorpseObjectTypeID` |
| `+0x847C` | `skinnedCorpseObjectTypeID` |
| `+0x8488` | `weaponData` |
| `+0x84B8` | `animalTypeId` |

The corpse spawner reads `+0x8478` and instantiates that object type as the carcass.

## Hooking the spawn: `AnimalCreate` / `AnimRemap`

`hooks_engine.cpp` attaches a hook on `CmOffset::CREATE_ANIMAL` → `Hooks::AnimalCreate::OnCreateAnimal`. After the engine builds the animal, the hook tags every type-`755` instance via `Hooks::AnimRemap::Register(animal)` and records the latest as `LastBandit` (consumed by `Lifx::bindLastAnimal`). Tagging at the factory means our death / animation hooks apply **however** the animal spawned — GM `/animal BanditData`, a wild `cm_spawn_patterns.xml` spawn, or `Lifx::spawnHostile`.

### `setAnimation` must NOT be suppressed

`AnimatedNPC::setAnimation` = `CmOffset::ANIMATED_NPC_SET_ANIMATION` = `0x2E2A90`. It is **functionally required** — suppressing it froze even a valid-anim `WolfData` (no movement, unkillable). The wolf AI tree drives locomotion/combat *through* `setAnimation`; `male.dts` lacks the wolf sequence names, so an unmapped call would log *"can't find animation %s"* and no-op (the animal still functions — the move-engine drives position; animation is cosmetic).

`Hooks::AnimRemap::OnSetAnimation` is therefore **not** a pass-through — it is a wolf→male sequence-name remap that re-runs the engine resolve→set chain with the mapped name. **Verified in-game (2026-06-26, #154 Stage 0):** with the remap live, 272 attack swings fired with **zero** resolve misses — `Attack_Power → hit1H_power_slash_fire` (seq idx `326`) and `Attack_Fast → hit1H_leftright_fire` (seq idx `204`), plus `idle1` (`0`) and `Run` (`2`). So the `male.dts` swing/idle/run sequence names are confirmed real (despite `male.dts` being LFXE-encrypted — see [`dts_encryption.md`](dts_encryption.md)), and the bandit visibly plays a one-handed swing. Note this is animation only: the swing resolves no hit (the damage calculator `0x0A4BF0` never fires) — held-weapon render + strike are tracked in #154.

## Death → carcass vs. tombstone

By default `/kill` → `Player::onRemove(class: Animal)` → wolf **carcass**, spawned by a death-EVENT chain rather than the animal's own vtable. Two things to know:

- The "Player vtbl slot 44 / 48" labels (`SPAWN_LOOTSTONE` = `0x102570`, `ON_DEATH_HAPPENS` = `0x0FB390`) are for the **Player** vtable and are **WRONG for the Animal vtable**: on the Animal vtable, slot 44 = `0x434250` (a field serializer) and slot 48 = `0x42F010` (a `getClassName` debug-printer). Overriding them does NOT redirect death and **corrupts** the animal (breaks damage sync → unkillable by weapons). The first tombstone attempt did exactly this; it was disabled.
- `Animals::SpawnControl::DeathHandler` vtable is at RVA `0x79D170` (meta_ptr `0x79D168`); its slot-0 (`0x19A500`) is only the dtor thunk. This subscribes to animal death but the corpse-spawn itself is deeper.

### The carcass chokepoint

`Animals::Animal::createCorpse` = `CmOffset::ANIMAL_CREATE_CORPSE` = `0x18A370` — **THE** carcass chokepoint (reads datablock `+0x8478`, creates the carcass, removes the animal from the manager). `Hooks::AnimalDeath::OnCreateCorpse` hooks it:

1. Non-hostile (real wild animal, fails `AnimRemap::IsHostile`) → call original `createCorpse`, untouched.
2. Hostile but **not** character-bound (`charStats+0x4A9 != 1`) → fall through to the normal carcass so loot is never lost.
3. Hostile **and** character-bound → suppress the carcass and fire the Player death trigger: `charStats->vtbl[0x130](charStats, 0)` (`CmOffset::CHARSTATS_DEATH_TRIGGER_SLOT` = `0x130`; the engine's own death router that calls this is `0x3BE890`). The cloned `charStats` vtable is a heap clone, so the death hook must NOT reject it for being out-of-module — only the death-trigger fn at `clone[0x130]` itself must be in-module.

Tail cleanup mirrors the original: `Animals::Manager` remove = `CmOffset::ANIMAL_MGR_REMOVE` = `0x196850`, called with the manager animal id at `animal+0x2540` (`CmOffset::ANIMAL_MGR_ID_OFF`).

The Player death path creates a lootstone of movable object type `1070` (the grave / tombstone container). The DB rejects `OwnerID = -2` (the unbound-animal sentinel) because `movable_objects.OwnerID` FKs `character.ID` — hence the character bind below.

## Character bind for a worn-loot tombstone

`Lifx::bindLastAnimal` mints a throwaway character and applies the #125 "Strategy-P" bind to the **Animal's** `charStats` (its vtable differs from the `NPCDecorative` one, so we clone whatever vtable the live instance holds and patch only the charID getter):

1. Mint a character via `CreateTestCharacter(accountId, charId)` — used `accountId = 0x30000000`, `charId = 0x30000001 + counter`. It inserts the account + character rows so `charId` is a valid `character.ID` for the `OwnerID` FK.
2. Clone the live `charStats` vtable (113 method slots = `0x388` bytes, plus the RTTI slot → `0x390` allocated) into RW memory; patch slot `+0x08` (the per-class charID getter) → a reader that returns `*(u32*)(charStats+0x109C)`.
3. Write `charId` into `charStats+0x109C` (the engine's own charID field).
4. Set the character-backed flag `charStats+0x4A9 = 1`.
5. Flush the equip cache: `charStats+0x498 = 0` (cached `CmPlayerEquipment*`) and `charStats+0x4A0 = 0` (cached refcount control block), so the next getter call rebuilds.

| `charStats` offset | Meaning |
|---|---|
| vtbl slot `+0x08` | charID getter (per-class) |
| `+0x498` | cached `CmPlayerEquipment*` |
| `+0x4A0` | cached refcount control block |
| `+0x4A9` | "is character-backed" flag (1 byte) |
| `+0x109C` | charID field |
| vtbl slot `+0x130` | Player death → lootstone trigger (`CHARSTATS_DEATH_TRIGGER_SLOT`) |

The `charStats` sub-object sits at `animal+0xAA8`; the `NPCDecorative` charStats vtable (used to distinguish an Animal from an NPCDecorative) is at RVA `0x7E4388`.

### `CREATE_TEST_CHARACTER` — offset conflict

> **CONFLICT — verify before reuse.** `cm_offsets.h` defines `CmOffset::CREATE_TEST_CHARACTER = 0x1D29B0` (commented *"wraps `Character::Create`; full DB-side char"*). The **deployed** bind code (`source/server/api/lifx_hostile.cpp:989`) and the original RE notes instead use `kCreateTestCharRva = 0x1D1670`, with signature `unsigned long long __fastcall(u32 accountId, u32 charId)`. The two differ; `0x1D1670` is the one actually shipped and validated. They are likely an inner worker vs. an outer wrapper, but this has not been reconciled — do not assume `CREATE_TEST_CHARACTER` and the deployed RVA are interchangeable.

The bound character's tombstone is **empty** until items are equipped on it; the worn-loot / equip-render path (and the cci-free SQL loot move) live in the equipment offsets in `cm_offsets.h` (`EQUIP_*`, `CONTAINER_TRYINIT`, `DB_EXEC_FORMATTED`) — see [`hostile-npc-ai-path-comparison.md`](hostile-npc-ai-path-comparison.md).

## Validated in-game flow

```
/animal BanditData          # engine-integrated spawn: moves, chases, killable by weapons
Lifx::bindLastAnimal()      # mint char + Strategy-P bind on the animal charStats
<kill it>                   # carcass suppressed -> real Player tombstone (valid OwnerID)
```

`Lifx::spawnHostile(charID [, animalTypeId=755])` (`source/server/api/lifx_hostile.cpp`) is the RE-probe path: it `createAnimal`s beside a connected character and echoes the `SimObjectId`, but a raw `createAnimal` + `setTransform` spawn renders/faces yet never integrates (unhittable/immobile) — use the engine spawn for anything real.

## Offset reference (cross-checked against `cm_offsets.h`)

| Symbol | RVA / value | `CmOffset` constant |
|---|---|---|
| `Animals::Manager::createAnimal` | `0x195FD0` | `CREATE_ANIMAL` |
| `Animals::Manager` singleton ptr | `0xB80C90` | `ANIMALS_MANAGER_GLOBAL_RVA` |
| `AnimalData::GetDatablockByTypeID` | `0x18C790` | `ANIMAL_GET_DATABLOCK_BY_TYPE` |
| `Animals::Animal` vtable | `0x798AA0` | `ANIMAL_VTABLE` |
| `SimObject` id offset | `0x90` | `SIMOBJECT_ID_OFF` |
| `Animals::Animal::createCorpse` | `0x18A370` | `ANIMAL_CREATE_CORPSE` |
| `Animals::Manager` remove | `0x196850` | `ANIMAL_MGR_REMOVE` |
| manager-id offset | `0x2540` | `ANIMAL_MGR_ID_OFF` |
| death-trigger vtbl slot | `0x130` | `CHARSTATS_DEATH_TRIGGER_SLOT` |
| `AnimatedNPC::setAnimation` | `0x2E2A90` | `ANIMATED_NPC_SET_ANIMATION` |
| `CreateTestCharacter` (deployed) | `0x1D1670` | — (conflicts with `CREATE_TEST_CHARACTER = 0x1D29B0`) |

These RVAs have **no** named `CmOffset` constant and live only in the RE notes / inline comments: Animal ctor `0x188EF0`, set-datablock `0x133B80`, `registerObject` `0x4304A0`, `AnimalData::initPersistFields` `0x18C930`, `DeathHandler` vtable `0x79D170` (meta `0x79D168`, slot-0 dtor thunk `0x19A500`), Animal-vtable slot 44 `0x434250` / slot 48 `0x42F010`, death router `0x3BE890`, NPCDecorative charStats vtable `0x7E4388`, hash-map globals `DAT_140b81108` / `DAT_140b81100`.

## Status & provenance

- **Runtime-verified:** the engine-spawned `BanditData` (AnimalData, `male.dts`, type `755`) moves, chases, and is killable by player weapons; the `createCorpse` redirect produces a real Player tombstone on a character-bound bandit; `CreateTestCharacter` at `0x1D1670` mints a usable `character.ID`. The `createAnimal` factory, manager singleton, `SimObjectId`, and the charStats bind offsets (`+0x4A9`, `+0x109C`, `+0x498`, `+0x4A0`, getter slot `+0x08`, death slot `+0x130`) are exercised by shipped code in `lifx_hostile.cpp` / `hook_animal_*.cpp`.
- **Reverse-engineered, not all runtime-verified:** the `AnimalData` datablock field offsets (`+0x8458`…`+0x84B8`), the FNV-1a hash-map internals, the `DeathHandler` vtable chain, and the Animal-vtable slot-44/48 identities come from static Ghidra analysis of `ddctd_cm_yo_server.exe` (ImageBase `0x140000000`).
- **Done since:** the wolf→male sequence-name remap is shipped and runtime-verified (#145; see [setAnimation section](#setanimation-must-not-be-suppressed)); populating the bound character's equipment so the tombstone carries worn loot is shipped and verified (#145, [`lootstone_injection.md`](lootstone_injection.md)).
- **Open spikes:** registering a *distinct* Bandit `animalTypeId` (hash map + object-type tree root `751`); giving the bandit a real **held weapon** that renders in-hand, swings, and deals damage — the native swing currently plays the animation but resolves no hit (#154).
- **Conflict:** `CREATE_TEST_CHARACTER` in `cm_offsets.h` (`0x1D29B0`) disagrees with the deployed/validated RVA (`0x1D1670`); reconcile before relying on the header constant.
