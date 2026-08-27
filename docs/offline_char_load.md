---
title: Offline character load
status: re
domain: reverse-engineering
tags: [character-info, equipment, lootstone, main-thread, hostile-npc]
related: [character_hp.md, ai_and_spawning.md, hostile-npc-ai-path-comparison.md, effects_and_abilities.md]
sources: [source/server/cm_offsets.h, source/server/api/lifx_hostile.cpp, hostile-npc-ai-path-comparison.md, character_hp.md]
updated: 2026-06-26
---

# Offline character load

Build a **fully DB-loaded `CmCharacterInfo`** for a bare `charId` with **no `GameConnection`, no client login, no map**. The engine's own connect-time loader `CHARACTER_LOAD_INMEM` (`0x1BB290`) takes a single `CmCharacterInfo*`, reads the `charId` from `cci+0x358`, and runs the full stats / inventory / equipment load against the world DB. The only non-obvious requirements are (a) a hand-built ref-count block to stand in for the `shared_ptr` the engine would normally hold, and (b) running the construction **on the engine main thread** — the field-init ctor registers thread-local signal state that faults on a worker thread. This is the foundation for the `#145` worn-loot tombstone (load a minted bandit's gear into memory so the death loot-resolver can drop it).

## The blocker that wasn't

The earlier assumption — that a `CmCharacterInfo` (cci) is irreducibly coupled to a `GameConnection` and can only exist for a logged-in client — was **wrong**. `CHARACTER_LOAD_INMEM` = `FUN_1401bb290(cci)` is the *complete* connect-time loader and takes **only** a `CmCharacterInfo*`. It needs no connection and no loaded map. Everything it does keys off the `charId` stored at `cci+0x358`.

What it runs, in order:

| Step | Function | What it does |
|---|---|---|
| stats | `CharacterParameters::loadFromDb` | stats + `RootContainerID` / `EquipmentContainerID`; `SELECT ... FROM `character` c JOIN account ... WHERE c.ID=%u` |
| inventory alloc | `CmInventoryPlayer::init` (`FUN_1402900b0` = `CM_INVENTORY_PLAYER_INIT 0x2900B0`) | allocs `CmPlayerEquipment` @ `record+0x48` + inventory @ `record+0x40`, builds the root container, calls `CmPlayerEquipment::initialize` |
| inventory load | inventory `loadFromDb` (`FUN_1402905c0`) | populates inventory items |
| equipment load | `CmPlayerEquipment::loadFromDb` (`FUN_1401f2760` = `EQUIP_LOAD_FROM_DB 0x1F2760`) | populates equipped slots from SQL `equipment_slots` (needs `equip+0x50` player_id + `equip+0x58` container) |
| HP | HP DB sync | syncs hard/soft HP from DB |

`FUN_1402905c0` (inventory `loadFromDb`) is an internal call inside `CHARACTER_LOAD_INMEM` and is **not** broken out as a named constant in `cm_offsets.h` — you never call it directly; the one-shot loader does.

The DB rows it reads (containers, starting items, `equipment_slots`) are created up front by `CREATE_TEST_CHARACTER` (`0x1D29B0`, `U32 __fastcall(U32 accountId, U32 charId)`, wraps `Character::Create` → `f_createInventory` / `f_createEquipment` / `f_insertNewItemInventory` / `p_allocate_equipment_slots` + `INSERT character`).

## Building a standalone cci

The engine normally hands `CHARACTER_LOAD_INMEM` a cci that lives inside a `std::_Ref_count_obj<CmCharacterInfo>` (i.e. a `make_shared` block: ref-count header followed by the object). To build one offline, mirror the alloc in `FUN_14028b050`:

```c
refc = ENGINE_ALLOC(0x3a8)              // 0x6DF950, void* __fastcall(size_t)
memset(refc, 0, 0x3a8)                  // REQUIRED — see "memset" note below
*(u64*)refc        = base + 0x7CFD98    // CCI_REFCOUNT_VFTABLE_RVA: _Ref_count_obj<CmCharacterInfo>::vftable
*(u32*)(refc+0x08) = 1                  // strong refcount
*(u32*)(refc+0x0C) = 1                  // weak refcount
cci = refc + 0x10                       // the CmCharacterInfo itself

FUN_1401b9c50(cci, charId)              // 0x1B9C50 CCI_FIELD_INIT_CTOR — field-init; stores charId @ cci+0x358 (NO DB)
FUN_1401bb290(cci)                      // 0x1BB290 CHARACTER_LOAD_INMEM — full DB load

// then walk the result:
record    = cci    + 0x370              // CCI_RECORD_OFF (== cci[0x6e])
inventory = record + 0x40               // RECORD_INVENTORY_OFF -> CmInventoryPlayer*
equip     = record + 0x48               // RECORD_EQUIP_OFF      -> CmPlayerEquipment*
```

The `0x3a8`-byte block is the full size of the ref-count object (`0x10` header + the `CmCharacterInfo`). `cci = refc + 0x10` skips the strong/weak counts.

**memset is mandatory.** The field-init ctor uses move-assignment: it reads and decrefs the *old* value of each field before overwriting. The engine's own allocator returns zeroed memory; `ENGINE_ALLOC` does not, so without the `memset(refc, 0, 0x3a8)` the ctor decrefs garbage pointers and crashes. memset was necessary but **not** sufficient — the second crash was the thread issue below.

## Reading the loaded equipment

After the load, equipped items live in the slot container:

| Offset | Field |
|---|---|
| `equip+0x50` | `CharacterID` (player_id) — `EQUIP_PLAYER_ID_OFF` |
| `equip+0x58` | root/slot container (`CmEquipmentContainer`) — `EQUIP_CONTAINER_OFF` |
| `container + 0x100 + slot*8` | slot's value: `itemId` (low 32) + `skinId` (high 32) — `EQUIP_SLOT_BASE_OFF`, slots `1..0x11` |
| `record+0x50` | `_Ref_count_obj<CmPlayerEquipment>*` — `RECORD_EQUIP_REFC_OFF` |

Writing a slot goes through `_setSlot` (`0x1EE930`, low-level write to `container+0x100+slot*8`) or the high-level `setSlot` (`EQUIP_SET_SLOT_HIGH 0x1F38D0`, `(equip, u8 slot, u64 itemId(+skinId hi32), int)` — validates, writes the slot, persists to SQL `equipment_slots` via `EQUIP_SET_SLOT_DB 0x1EEA40`, and fires `EquipTriggerSignal`).

## The main-thread gotcha (the second crash)

The cci field-init ctor `FUN_1401b9c50` registers **thread-local signal state that is valid only on the engine main thread**. A LiFx console command runs on a **worker thread** (its `[tid]` in the log differs from the `{03}` game thread), and building there faults *inside the ctor*, right after the `CmCharacterWounds::CmCharacterWounds()` trace.

This is confirmed structurally: the engine's own char-load entry `FUN_1401bab40` gates on `FUN_140407ef0` (= `IS_MAIN_THREAD 0x407EF0`, `bool __fastcall()`, true iff the caller is the main thread — it caches the *first* caller's `_Thrd_id`) and marshals the work via an async `MainThreadEvent`.

`IS_MAIN_THREAD` cannot be used as a live predicate from LiFx here: with no player logins the engine never primes it (it caches whichever thread asks first). So the implementation **always** marshals through the Sim `schedule()` queue — the same main-thread pump `ensureSpawns` / NPC-create use:

```ts
function lifxLoadEquip(%c){ Lifx::loadBoundEquip(%c, 1); }
schedule(0, 0, lifxLoadEquip, <charId>);
```

The scheduled re-entry passes a second arg `=1` so the callback knows it is the main-thread call and builds directly instead of re-marshalling.

## Loot path needs no global registration

The death/loot path does **not** require the cci to be registered in any global char manager. The engine's global char manager only registers connection-driven logins, so a minted bandit char has no cci there — but the loot path takes the pointers directly:

- `Player::spawnLootstone` = `FUN_140102570` (`SPAWN_LOOTSTONE 0x102570`, Player vtbl **slot 44**) — the corpse-container builder. It reaches the Player via `charStats - 0xAA8` (the inverse of the `NPCS::Base` layout where `charStats` sits at `self+0xAA8`). The loot transfer chain is `0x102770 -> 0x1F2DA0`.
- The loot/listener resolver `FUN_140090ab0` takes a `CmPlayerEquipment*` **and** a `CmCharacterInfo*` as **direct pointer args** (it logs `player %u has null CmPlayerEquipment/CmCharacterInfo/inventory`).

So the dying entity only needs a pointer chain to a populated `equip` + `cci`. No registration step. Death itself routes through `ON_DEATH_HAPPENS 0x0FB390` (Player vtbl slot 48, shared by `NPCDecorative`) and the death router `0x3BE890`, which calls the charStats death trigger at vtbl slot `+0x130` (`CHARSTATS_DEATH_TRIGGER_SLOT`) as `(charStats, 0)`.

## Where this lives

Implemented in `source/server/api/lifx_hostile.cpp`:

- `BuildLoadedCci(charId)` — does the alloc/memset/ctor/load above, caches the result in `g_builtCci` (a `charId -> cci*` map guarded by `g_builtCciMtx`).
- `LookupCci(charId)` — tries `Engine::Character_GetByID` (a real login) first, then the `g_builtCci` cache.
- `LoadBoundEquipCore(charId, onMainThread)` — off-main-thread it schedules itself; on-main-thread it builds (or, for an already-present cci, materializes inventory+equip via `CM_INVENTORY_PLAYER_INIT` then `EQUIP_LOAD_FROM_DB`), then dumps state.
- Console commands: `Lifx::loadBoundEquip(charId)` (drives the build) and `Lifx::dumpBoundEquip(charId)` (read-only probe of what's loaded — cci, record, inventory/equip pointers, and the non-empty slots). Both are fully `VirtualQuery`-guarded.

## Remaining work for the worn-loot tombstone

Two pieces are still open for an actual gear-dropping corpse:

1. **The DB equipment is empty.** `CREATE_TEST_CHARACTER` puts the starting items in the **inventory**, not equipped, so `equipment_slots` is empty and nothing drops as worn gear. Items must actually be equipped via `setSlot` (`0x1F38D0`) — which first needs the item instance physically present in the equip container.
2. **Wiring the built cci to the dying entity.** `spawnLootstone`'s loot-fill must read the populated `equip`+`cci`, which means hanging them off the dying `Animal`'s `charStats`/Player chain. This is coupled to the Animal-vs-Player object layout and is shared with the Step 3 visible-gear work.

Note the related correction in `cm_offsets.h`: the equipment-from-entity resolver `EQUIP_ACCESSOR 0x28BD30` is **not** a `charStats` hash-map lookup — it is a generic registry lookup on the global type/registry singleton `*0x140B53908` (`EQUIP_REGISTRY_GLOBAL_RVA 0xB53908`), keyed by a value from a virtual call (vtable slot 1) on the entity. The earlier `CHARSTATS_EQUIP_KEY_OFF 0xB14` was wrong and has been removed. The exact key the loot path `0x1F2DA0` uses, and the handle→`CmPlayerEquipment` chain, still need a clean static RE pass before piece (2) is wired.

## RVA / offset reference

All named constants live in `source/server/cm_offsets.h` (`enum CmOffset`); RVAs are relative to image base `0x140000000`.

| Symbol / constant | Value | Meaning |
|---|---|---|
| `ENGINE_ALLOC` | `0x6DF950` | `void* __fastcall(size_t)` engine allocator (returns NON-zeroed memory) |
| `CCI_REFCOUNT_VFTABLE_RVA` | `0x7CFD98` | `_Ref_count_obj<CmCharacterInfo>::vftable`, stored at `*refc` |
| `CCI_FIELD_INIT_CTOR` (`FUN_1401b9c50`) | `0x1B9C50` | cci field-init ctor `(cci, u32 charId)`; stores charId @ `cci+0x358`; no DB; main-thread-only |
| `CHARACTER_LOAD_INMEM` (`FUN_1401bb290`) | `0x1BB290` | full connect-time load from cci; connection-FREE (verified 2026-06-25) |
| `IS_MAIN_THREAD` (`FUN_140407ef0`) | `0x407EF0` | `bool __fastcall()`; caches first caller's `_Thrd_id` |
| `CM_INVENTORY_PLAYER_INIT` (`FUN_1402900b0`) | `0x2900B0` | `CmInventoryPlayer::init(record, cci)` |
| `EQUIP_LOAD_FROM_DB` (`FUN_1401f2760`) | `0x1F2760` | `CmPlayerEquipment::loadFromDb(equip)` |
| inventory `loadFromDb` (`FUN_1402905c0`) | `0x2905C0` | internal call inside `CHARACTER_LOAD_INMEM`; not a named constant |
| char-load entry (`FUN_1401bab40`) | `0x1BAB40` | engine's gated entry; checks `IS_MAIN_THREAD`, marshals via async event |
| cci alloc template (`FUN_14028b050`) | `0x28B050` | the alloc this recipe mirrors |
| `CREATE_TEST_CHARACTER` | `0x1D29B0` | `U32 __fastcall(accountId, charId)`; creates DB-side char rows |
| `SPAWN_LOOTSTONE` (`FUN_140102570`) | `0x102570` | Player vtbl slot 44; corpse container; Player via `charStats-0xAA8`; transfer `0x102770 -> 0x1F2DA0` |
| loot/listener resolver (`FUN_140090ab0`) | `0x90AB0` | takes `CmPlayerEquipment*` + `CmCharacterInfo*` as direct args |
| `ON_DEATH_HAPPENS` | `0x0FB390` | Player vtbl slot 48; death orchestrator (shared by `NPCDecorative`) |
| death router | `0x3BE890` | calls charStats death trigger `(charStats, 0)` |
| `CHARSTATS_DEATH_TRIGGER_SLOT` | `0x130` | charStats vtbl slot for the death→lootstone+worn-loot trigger |
| `EQUIP_SET_SLOT_HIGH` / `_FN` | `0x1F38D0` | `setSlot(equip, u8 slot, u64 itemId(+skinId hi32), int)` |
| `_setSlot` | `0x1EE930` | low-level write to `container+0x100+slot*8` |
| `EQUIP_SET_SLOT_DB` | `0x1EEA40` | persists a slot to SQL `equipment_slots` |
| `EQUIP_ACCESSOR` | `0x28BD30` | registry lookup on `*0x140B53908`; NOT a charStats lookup |
| `EQUIP_REGISTRY_GLOBAL_RVA` | `0xB53908` | registry/type-manager singleton (accessor arg1) |

**Field offsets**

| Offset | On | Field |
|---|---|---|
| `+0x358` | cci | `charId` (`CCI_CHARID_OFF`) |
| `+0x370` | cci | `record` (`CCI_RECORD_OFF`, == `cci[0x6e]`) |
| `+0x40` | record | `CmInventoryPlayer*` (`RECORD_INVENTORY_OFF`) |
| `+0x48` | record | `CmPlayerEquipment*` (`RECORD_EQUIP_OFF`) |
| `+0x50` | record | `_Ref_count_obj<CmPlayerEquipment>*` (`RECORD_EQUIP_REFC_OFF`) |
| `+0x50` | equip | `CharacterID` / player_id (`EQUIP_PLAYER_ID_OFF`) |
| `+0x58` | equip | slot container (`EQUIP_CONTAINER_OFF`) |
| `+0x100 + slot*8` | container | slot value `itemId`(lo32)+`skinId`(hi32), slots `1..0x11` (`EQUIP_SLOT_BASE_OFF`) |
| `+0xAA8` | `NPCS::Base` self | `charStats` (`spawnLootstone` reaches Player via `charStats-0xAA8`) |
| `+0x08` / `+0x0C` | refc block | strong / weak refcount |
| `+0x10` | refc block | the `CmCharacterInfo` (`cci = refc + 0x10`) |

## Status & provenance

- **Verified:** `CHARACTER_LOAD_INMEM` is connection-free and is the full connect-time loader — verified 2026-06-25 (also annotated as verified in `cm_offsets.h`). The two failure modes during bring-up (decref-of-garbage without the memset; ctor fault on a worker thread) were both observed and resolved at runtime. All RVAs/offsets here cross-check against `source/server/cm_offsets.h`; none conflict.
- **RE / inferred, not yet end-to-end runtime-confirmed:** the complete offline build (`BuildLoadedCci`) plus the worn-loot tombstone was "awaiting in-game test as of build `3e947630`." The two remaining-work items (empty `equipment_slots`; wiring the built cci into the dying Animal so `spawnLootstone` reads it) are open, and the loot-resolution key/handle chain (`0x1F2DA0`) still needs a clean static RE pass after the `EQUIP_ACCESSOR 0x28BD30` correction.
- Object layout context (`NPCS::Base` charStats @ `+0xAA8`, Player vtbl slots) is shared with the hostile-NPC AI path comparison and the character-HP work; see the related docs.
