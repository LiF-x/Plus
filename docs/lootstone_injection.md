---
title: Lootstone item injection
status: re
domain: reverse-engineering
tags: [loot, containers, death-pipeline, hostile-npc, sql]
related: [hostile-npc-ai-path-comparison.md, ai_and_spawning.md, character_hp.md]
sources: [source/server/cm_offsets.h, source/server/api/lifx_hostile.cpp, source/server/api/lifx_hostile.h, source/server/hooks/character/hook_animal_death.cpp, source/server/hooks/character/hook_container_init.cpp]
updated: 2026-06-29
---

# Lootstone item injection

How to put items into a LiF death-loot container (a "lootstone" / grave), and why doing it for a
*connection-less, never-logged-in* bandit character (issue #145 worn-loot tombstone) is hard. Every
in-engine item-add path bottoms out on a live `CmCharacterInfo` (cci) or a `GameConnection` that a
synthetic bandit lacks. The shipped workaround skips all of them: it moves the bandit's existing
character-container item rows into its grave's container **directly in the world DB** using the
engine's own async SQL exec primitive, then forces the grave's in-memory container to reload so the
loot is visible live without a restart.

## The problem

A character-bound hostile NPC (an `Animals::Animal` whose `charStats` was bound to a minted charId —
see the #125 bind, `charStats+0x4A9 == 1`, charId at `charStats+0x109C`) is killed. We want its worn
gear to drop in a player-style tombstone. The minted bandit (`charId 805306369`) already has a
**complete DB-side state** from `Character::Create` (`f_createInventory` / `f_createEquipment` /
`f_insertNewItemInventory` / `p_allocate_equipment_slots` + `INSERT character`): real container rows,
starting items, equipment_slots. What it never has is an *in-memory* cci or a network connection — and
that is exactly what every engine loot path requires.

## The death → loot pipeline (engine internals)

The Player death trigger is reached through the dying entity's `charStats` vtable. The redirect hook
(`Hooks::AnimalDeath::OnCreateCorpse`) suppresses the stock animal carcass and instead fires the same
trigger the engine's death router uses for a Player: `charStats->vtbl[0x130](charStats, 0)`.

| Symbol | RVA | Notes |
|---|---|---|
| death router | `0x3BE890` | engine calls `(charStats, 0)` here |
| `CHARSTATS_DEATH_TRIGGER_SLOT` | `0x130` | `charStats.vtbl[+0x130]` → `0x89B40`; the Player death → lootstone+worn-loot trigger |
| (death trigger fn) | `0x89B40` | `charStats` vtbl slot `0x130`; calls slot `0x138` |
| `CharacterParameters::Zed_is_dead` | `0x89660` | `charStats` vtbl slot `0x138` (`characterfalls.cpp`); does `cci = charStats->vtbl[0x40](charStats); if(!cci) error "character info not found"` — grave/inventory association happens **after** and **depends on** that cci |
| (char-info getter) | `charStats` vtbl slot `0x40` | returns the in-memory cci (null for our bandit) |
| `CharacterParameters::vftable` | `0x738A80` | the vtable the three slots above live in |
| `SPAWN_LOOTSTONE` (`Player::spawnLootstone`) | `0x102570` | Player vtbl **slot 44**; creates the empty stone. Inner loot-transfer at `0x102770` → loot path (see conflict note below) |
| `ON_DEATH_HAPPENS` | `0x0FB390` | Player vtbl **slot 48**; death orchestrator, shared by `NPCDecorative` |
| `Player::lootPlayer` | `0x38C210` | reached from `spawnLootstone`; **connection-gated** — bails `"NetConnection have no GameConnection"` / `"...have no Player"` |

In-game finding (build `6e6482d7`): for the connection-less bandit, `spawnLootstone` (`0x102570`) is
**never called** at all. The death path stops inside `Zed_is_dead` when the cci lookup fails. With a
valid bound charId the **grave row does spawn** (`movable_objects`, `ObjectTypeID=1070`, `OwnerID=charId`
— FK passes) but it is **empty**: the item transfer into the grave container is the cci-gated step the
engine skips.

## Item-add paths surveyed

Every engine entry point for "add an item to a container", and why each is unusable for a bandit:

| Path | RVA(s) | Verdict |
|---|---|---|
| `Player::inventoryAddItem(itemType, qty, quality, durability, createdDurability)` (TS console) | `0xF79B0` → `0xF1250` → `cAddInventoryItemsTransaction` builder: ctor `0x29B630`, add `0x29C030`, commit `0x29C2D0`, dtor `0x29BB50` | **cci-gated.** The transaction resolves a `CmCharacterInfo` by id via the registry accessor `0x28BD30(*0x140B53908, ...)` and adds to *that* char's root container. Errors `"No inventory"` if the cci is missing. |
| `CmContainerItem::addItemInContainer(container, itemObject)` | `0x279FD0` | **cci-free.** Adds an existing item *object* to a container directly. Needs an item object first (next two rows). |
| `createItemFromDbResult(row)` | `0x294CB0` | Builds an item object from a DB row. Feeds `addItemInContainer`. |
| `CmServerInventoryContainer::tryInit` (`CONTAINER_TRYINIT`) | `0x299140` | Container DB-load: `SELECT ID, ObjectTypeID, Quality, Quantity, Durability... FROM items WHERE ContainerID=mID`, builds each item via `createItemFromDbResult` and adds it (vtbl slot `0x90`). Guarded by the init flag at `+0x14` (loads once, then caches). |
| `CmServerInventoryContainer::addNewItem(container, descriptor, tryStack)` | `0x2939A0` | Low-level add; returns itemId. Descriptor is ~`0x50` bytes (layout below). |
| `insertItemsInDB` | `0x296950` | Persists added items to SQL. |

The only fully cci-free / connection-free engine combination is `createItemFromDbResult` +
`addItemInContainer` — but it needs the **grave's** container pointer, which is created *inside* the
large cci-dependent `Zed_is_dead`. So even that bottoms out on the same missing state.

## Struct layouts

`CmServerInventoryContainer` (verified in the `tryInit` decomp):

| Off | Type | Field |
|---|---|---|
| `+0x14` | u8 | init guard — `0` ⇒ `tryInit` loads; nonzero ⇒ no-op (cached) |
| `+0x18` | u32 | `mID` (container's own id) |
| `+0x1c` | u32 | `ObjectTypeID` (grave = `1070`) |
| `+0x20` | u32 | `ParentID` |
| `+0x24` | u16 | `Quality` |

`addNewItem` descriptor (~`0x50` bytes, plus embedded sub-objects):

| Off | Field |
|---|---|
| `+0xc` | `ObjectTypeID` |
| `+0x10` | quantity |
| `+0x18` | durability |
| `+0x20` | quality |

## The shipped solution — cci-free SQL move + live reload

The bandit's items are already real `items` rows in its character containers; the grave is already a
real `movable_objects` row with its own root container. The gap is purely that `items.ContainerID`
still points at the character's containers instead of the grave's. Close it in the DB.

### Live DB facts (validated against `lif_world_d`, 2026-06-25)

- Bandit `character.ID = 805306369`: `RootContainerID = 17`, `EquipmentContainerID = 18`. (An earlier
  observation recorded the starting-items container as `ContainerID = 26`; the validated run used
  `17`/`18`.)
- Grave: `movable_objects` row, `ObjectTypeID = 1070`, `OwnerID = charId`, its own `RootContainerID`
  (e.g. `31`).
- Rolled-back-txn validation: **2 rows moved** (items `9` and `10`: container `17` → `31`).

### The UPDATE (movable_objects subquery path, `Lifx::dropBanditLoot`)

Moves the char's Root+Equipment items into the **freshest** grave's container:

```sql
UPDATE items i
JOIN character c
  ON (i.ContainerID = c.RootContainerID OR i.ContainerID = c.EquipmentContainerID)
JOIN movable_objects mo
  ON (mo.OwnerID = c.ID AND mo.ObjectTypeID = 1070 AND mo.RootContainerID IS NOT NULL)
SET i.ContainerID = mo.RootContainerID
WHERE c.ID = <cid>
  AND mo.ID = (SELECT MAX(m2.ID) FROM movable_objects m2
               WHERE m2.OwnerID = c.ID AND m2.ObjectTypeID = 1070);
```

Only `items` is the UPDATE target; `character` and `movable_objects` are read-only joins, so there is
no "target table specified twice" conflict. A second, faster variant (`FillBanditGraveCore` with a
known `targetMid`) skips the `movable_objects` subquery and writes straight into the container id the
container-init hook captured at grave creation: `... SET i.ContainerID = <targetMid> WHERE c.ID = <cid>`.

### Executing it through the engine's own async SQL primitive

Recovered from `Zed_is_dead`'s `chars_deathlog` INSERT:

| Symbol | RVA | Signature / behaviour |
|---|---|---|
| `DB_GET_WORLD_CONN` | `0x54CAF0` | `void* __fastcall(U32 idx)` — returns `(&DAT_140BF86F0)[idx]`; `idx 1` = the world-DB connection; `idx > 4` → null |
| `DB_EXEC_FORMATTED` | `0x0884F0` | `U8 __fastcall(void* conn, const char* fmt, const U32* a, const U32* b)` — does `snprintf(buf, n, fmt, *a, *b);` then `conn->vtbl[0](conn, buf)` |

`DB_EXEC_FORMATTED` **always dereferences both `a` and `b`**, even when `fmt` has no `%`-specifiers.
So: pre-format the entire statement yourself (keep `'%'` out of it) and pass two valid dummy `U32`
pointers. The shipped code formats into a `char sql[768]` and passes `&dummy, &dummy`.

### Wiring (event-driven, the version that ships)

Sequence, from the death hook through to live loot:

1. `Hooks::AnimalDeath::OnCreateCorpse` (`hook_animal_death.cpp`): only redirects if the animal is
   char-bound (`charStats+0x4A9 == 1`). Reads the bound charId at `charStats+0x109C`, calls
   `Lifx::Api::Hostile::ArmGraveFill(charId)` **before** firing `charStats->vtbl[0x130](charStats, 0)`,
   then does `createCorpse`'s tail cleanup (`Animals::Manager::remove`).
2. The Player death trigger creates the grave (`movable_objects`, type `1070`) and `tryInit`s its loot
   container **empty** (transfer is cci-gated, skipped).
3. `Hooks::ContainerInit::OnTryInit` (`hook_container_init.cpp`) hooks `CONTAINER_TRYINIT`. After the
   real load it inspects `+0x1c`; on `ObjectTypeID == 1070` it records the grave container ptr + `mID`
   and calls `Lifx::Api::Hostile::OnGraveContainerCaptured(mid)`.
4. `OnGraveContainerCaptured` consumes the armed charId (atomic exchange ⇒ idempotent) and calls
   `FillBanditGraveCore(charId, mid)`: runs the UPDATE into that exact container, then
   `ReloadFreshestGraveContainer(mid)` clears the container's `+0x14` init guard and re-invokes the
   **original** `tryInit` trampoline (not the hook) so it re-reads `items WHERE ContainerID=mID`. The
   in-memory container was empty, so the reload cannot duplicate.

The capture-driven fill happens at the exact instant the tomb becomes openable, so the loot is present
on first open — a fixed delay raced the looter. A manual / fallback path exists too:
`Lifx::dropBanditLoot(<charId>)` (TS console) → `FillBanditGraveCore(charId, 0)` (movable_objects
subquery), and `Lifx::Api::Hostile::ScheduleBanditGraveFill(charId)` defers a `dropBanditLoot` by
`2500` ms via the engine `schedule()` so the grave row commits first.

## Conflicts & cross-checks

- **Inner loot-transfer RVA disagreement.** The private RE note records `spawnLootstone`'s inner
  loot-transfer as `0x102770 → 0xF2DA0` (`FUN_1400f2da0`). `cm_offsets.h` (`SPAWN_LOOTSTONE` comment,
  and the `EQUIP_ACCESSOR` correction block) records it as `0x102770 → 0x1F2DA0`. Treat the in-repo
  value **`0x1F2DA0`** as authoritative; the note's `0xF2DA0` looks like a dropped digit. Flagged, not
  yet re-confirmed by disasm.
- **`0x28BD30` naming.** The note describes `FUN_14028bd30(DAT_140b53908, ...)` (used by
  `inventoryAddItem`'s transaction) as "resolve `CmCharacterInfo` by id". In `cm_offsets.h` this is
  `EQUIP_ACCESSOR = 0x28BD30`, documented (with a CORRECTION block) as a **generic registry/type-manager
  lookup** on the same global `*0x140B53908` (`EQUIP_REGISTRY_GLOBAL_RVA = 0xB53908`), keyed by a value
  from a virtual call on the entity (vtable slot 1), `rdx` = a 16-byte out buffer. Same function, two
  call sites; the "by-id cci resolve" framing is the inventory-transaction usage. Passing `charStats`
  as arg1 crashes.
- RVAs confirmed against `cm_offsets.h` named constants: `SPAWN_LOOTSTONE 0x102570`,
  `ON_DEATH_HAPPENS 0x0FB390`, `CHARSTATS_DEATH_TRIGGER_SLOT 0x130`, `CONTAINER_TRYINIT 0x299140`,
  `DB_GET_WORLD_CONN 0x54CAF0`, `DB_EXEC_FORMATTED 0x0884F0`, `EQUIP_ACCESSOR 0x28BD30`,
  `EQUIP_REGISTRY_GLOBAL_RVA 0xB53908`. `Zed_is_dead 0x89660` matches the `cm_offsets.h` comment.
- RVAs present only in the note (no named `cm_offsets.h` constant): the `inventoryAddItem` chain
  (`0xF79B0`, `0xF1250`, transaction `0x29B630`/`0x29C030`/`0x29C2D0`/`0x29BB50`), `addItemInContainer
  0x279FD0`, `createItemFromDbResult 0x294CB0`, `addNewItem 0x2939A0`, `insertItemsInDB 0x296950`,
  `lootPlayer 0x38C210`, the death-trigger fn `0x89B40`, and `CharacterParameters::vftable 0x738A80`.

## Status & provenance

- **Runtime-verified:** the UPDATE moves the right rows (2 rows, `17 → 31`, items `9`/`10`) against the
  live `lif_world_d` DB in a rolled-back txn; `spawnLootstone`/`lootPlayer` is connection-gated and
  `Zed_is_dead` is cci-gated (both confirmed in-game on build `6e6482d7` — the grave spawns but is
  empty); for the bandit, `spawnLootstone` (`0x102570`) is never reached. The DB exec primitives
  (`DB_GET_WORLD_CONN` / `DB_EXEC_FORMATTED`) and the `CONTAINER_TRYINIT` guard/layout are confirmed in
  the decomp and shipped (build `208b1ceb`). Code: `source/server/api/lifx_hostile.cpp`,
  `source/server/hooks/character/hook_animal_death.cpp`,
  `source/server/hooks/character/hook_container_init.cpp`.
- **Runtime-verified (end-to-end, build `4af13b67`, 2026-06-26):** a bandit spawned via
  `/animal BanditData` + `Lifx::bindLastAnimal()`, then killed, drops a Player tombstone whose worn loot
  (rags + bread) is present on the **first** open. This confirms the event-driven fill
  (`ArmGraveFill` → `OnGraveContainerCaptured` → `FillBanditGraveCore` → `ReloadFreshestGraveContainer`)
  lands the items before the tomb is openable, **and** that a filled-then-reloaded grave container
  serves the loot to the looting client on first open — i.e. the grave-open path does re-read the
  container after the `+0x14` init-guard clear + `tryInit` re-run, with no client-side empty-ghost cache.
  (Shipped via PR #152, merge `6aa2bef`.)
- **Inferred / not fully runtime-verified:** the item-descriptor field offsets for `addNewItem`; and the
  inner loot-transfer RVA (`0x1F2DA0` vs the note's `0xF2DA0`).

## Mounted-image materialize — drop the bandit's worn kit (#175/#177, runtime-verified)

The SQL **move** above relocates items that already exist in the bandit char's containers. But the
native Animal bandit's real gear is **mounted images** (the held weapon via `MOUNT_MOVABLE_OBJECT`,
plus the armor it renders), which are *not* container items, so there is nothing to move. The shipped
fix for the native bandit therefore **materializes** the worn kit as fresh `items` rows in the grave:

- A per-bandit **kit** (`{ renderLoadout, dropItems[] }`) drives both the client render *and* the loot,
  so what it wears is what it drops. Current kit: render loadout 0 (Full Plate meshes) + items `556`
  (Nordic Sword) and `547`–`552` (Full Plate Helm/Breastplate/Vambraces/Gauntlets/Leggings/Greaves),
  all valid `objects_types` FK targets in `lif_world_d`. Types are recorded at mount (weapon) / bind
  (armor), snapshotted at death keyed by charId, and inserted on grave-container capture.
- Row template, verified against a live grave row: `Quality 50, Quantity 1,
  Durability = CreatedDurability = 30000, FeatureID NULL`; `items.ID` is AUTO_INCREMENT.

**Runtime-verified gotchas:**
- **DB rows + reload is only sufficient if the writes are ATOMIC.** A per-row `INSERT` loop + an
  *immediate* reload surfaced only the **first** item: `DB_EXEC_FORMATTED` commits async on the
  `DBIPrimary` thread, so the reload's `SELECT` ran before rows 2..N landed and then re-cached the
  partial set (all N rows confirmed present in the DB afterward — only row 1 visible in the tomb). A
  **single multi-row `INSERT`** (all-or-nothing) plus a deferred reload (`Lifx::reloadFreshestGrave`,
  scheduled ~1.2 s later, after the write commits) surfaces the full grave. Verified: a 7-item kit
  drops in full.
- **One shared owner character.** `CreateTestCharacter` hardcodes `Name='test-aab'` under a
  `UNIQUE(Name)` constraint, so only one such row can exist — minting a per-bandit id fails the name
  uniqueness, the character row is never created, and the grave's `OwnerID` FK then fails (`#1452`) →
  the bandit *vanishes* with no tomb at all. All bandits bind to the single `test-aab` id
  (`0x30000001`); because loot is materialized (not moved from that char's container), a shared
  `OwnerID` across graves is correct.
- **`Zed_is_dead() "character info not found"` is benign here.** The grave still spawns (its `OwnerID`
  FK passes off the existing character *row*); the engine's own cci-gated loot path bails, which is
  fine because we materialize the loot ourselves. (Contrast the *vanish* case above, where the row was
  missing entirely → FK failure.)
