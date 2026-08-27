---
title: Animal data model
status: re
domain: reverse-engineering
tags: [animal, objects-types, datablock, loot, spawn, mysql]
related: [ai_and_spawning.md, hostile-npc-ai-path-comparison.md, bloomery.md]
sources: [source/server/cm_offsets.h, source/server/hooks/character/hook_animal_death.h, source/server/api/lifx_hostile.cpp, docs/ai_and_spawning.md, docs/hostile-npc-ai-path-comparison.md]
updated: 2026-06-26
---

# Animal data model

**TL;DR.** Despite what `sql/new.sql` implies, the live LiF world DB stores almost
nothing animal-specific. Animal **combat stats** come from the `AnimalData`
TorqueScript **datablock**, not SQL. Animal **loot** is not a drop table — it is the
generic `recipe` / `recipe_requirement` (skinning/butchering) system applied to the
carcass container. Only the **object-type hierarchy** and **wild-spawn rules** are
external: types live in `objects_types` (animals are children of `751`, carcasses of
`624`), and spawns are driven by `data/cm_spawn_patterns.xml`. New animal/humanoid
types are added by registering the object type **in-engine** via
`LiFx::registerObjectsTypes`, never by an `objects_types` SQL insert. This page is the
source of truth for "where does each piece of animal data come from" — read it before
touching the `HostileNPCs` bandit, a new creature type, or any loot work.

## 1. What the live DB does and does not contain

The live `_c` shard connects to MySQL DB **`lif_world_c`** (per
`lif_server_320850_c/config_local.cs`: `NEWROOT`/`NEWPASS`, `127.0.0.1:3306` — see
the MySQL-access reference). Direct query (2026-06-16) of that DB shows:

- **`animal_breeds`, `animal_drop_items`, `animal_spawn_patterns` do not exist.**
  They are defined in `sql/new.sql` but were never created in this build. The seed
  schema diverges from the live schema here; do **not** assume those tables are
  available. Any design that reads/writes them is dead on this server.
- `objects_types` **is** present and fully populated (see §2).
- The generic `recipe` / `recipe_requirement` tables drive carcass processing (§4).

| Animal datum | Lives in | NOT in |
|---|---|---|
| Combat stats (weapon, hits, HP) | `AnimalData` datablock (§3) | any SQL table |
| Loot / drops | `recipe` + `recipe_requirement` (skinning) (§4) | `animal_drop_items` |
| Object-type identity & hierarchy | `objects_types` (§2) | — |
| Carcass type for a creature | `AnimalData.rawCorpseObjectTypeID` (datablock) (§3) | SQL |
| Wild placement / density | `data/cm_spawn_patterns.xml` (§5) | `animal_spawn_patterns` |

## 2. The `objects_types` hierarchy

The full `objects_types` tree is present in `lif_world_c`. Animals and their carcass
containers hang off two abstract parents:

| Type | ID | Parent |
|---|---|---|
| "Wild animals" (abstract parent) | `751` | — |
| Wolf | `755` | `751` |
| "Fresh corpses" (abstract parent) | `624` | — |
| Wolf Carcass | `915` | `624` |
| Wolf Carcass (skinned) | `928` | `624` |

Live `MAX(objects_types.ID)` was **`3016`** at query time — pick new IDs above that to
avoid collisions, but prefer registering in-engine (§6) so the engine assigns/validates
parentage rather than hand-editing SQL.

A creature's **datablock** is resolved by type ID at runtime through
`AnimalData::GetDatablockByTypeID(u32 typeId)` (`ANIMAL_GET_DATABLOCK_BY_TYPE`,
RVA `0x18C790`) — a typeId→`AnimalData*` hash map. So `objects_types.ID` (`755` for
Wolf) is the key that links the type row to its datablock.

## 3. Combat stats — the `AnimalData` datablock is the source of truth

Animal combat is **datablock-driven**, not SQL-driven. The authoritative fields on the
`AnimalData` datablock are:

- `weaponData`, `weaponWeight`
- `powerHit*` / `fastHit*` (the attack families)
- `maxHP`
- `rawCorpseObjectTypeID` — the carcass `objects_types` ID this creature spawns on
  death; read at **datablock `+0x8478`** (confirmed in
  `source/server/hooks/character/hook_animal_death.h`).

Corroborating runtime entry points (from `cm_offsets.h`, RE'd via Ghidra; see the
`reference_animal_spawn_re` provenance):

| Symbol (`CmOffset`) | RVA | Meaning |
|---|---|---|
| `CREATE_ANIMAL` | `0x195FD0` | `Animals::Manager::createAnimal(mgr, u32 typeId, u32 quality, int id[0=auto], u8 flag) -> Animal*`; resolves the datablock **by type ID** |
| `ANIMALS_MANAGER_GLOBAL_RVA` | `0xB80C90` | module-rel ptr to the `Animals::Manager` singleton (`createAnimal` arg1) |
| `ANIMAL_GET_DATABLOCK_BY_TYPE` | `0x18C790` | `AnimalData::GetDatablockByTypeID(typeId) -> AnimalData*` |
| `ANIMAL_VTABLE` | `0x798AA0` | `Animals::Animal` primary vtable (observed live) |
| `ANIMAL_MGR_ID_OFF` | `0x2540` | `animal+0x2540` = manager animal id |
| `SIMOBJECT_ID_OFF` | `0x90` | `SimObject` `mId` (u32), assigned by `registerObject` (`0x4304A0`) |
| — | `0x18B450` | `Animal::packUpdate` — ticks the behaviour tree on the pack thread |

`spawnObject` is dead for animals (no console class registered); creatures are
manager-only, instantiated via `createAnimal` keyed by `animalTypeId` (Wolf = `755`).
See `ai_and_spawning.md` for the full spawn-manager (`Animals::SpawnControl`) map.

## 4. Loot — recipe/skinning, not a drop table

There is **no `animal_drop_items` drop table**. Animal loot is the generic
`recipe` / `recipe_requirement` system applied to the **carcass container**: you skin
or butcher the carcass (`objects_types` child of `624`; Wolf Carcass `915` → skinned
`928`) and the recipe yields the items. The carcass is a normal container type — the
same `objects_types`-joined-`recipe` machinery documented in `bloomery.md`.

### Death routing (why the carcass appears)

The central death router (`0x3BE890`) RTTI-dispatches a dying entity:

- `Animals::Animal` → `Animals::Animal::createCorpse` (`ANIMAL_CREATE_CORPSE`,
  RVA `0x18A370`) — **the single carcass chokepoint**. It reads
  `AnimalData.rawCorpseObjectTypeID` at datablock `+0x8478`, creates that carcass
  container, then removes the animal from the manager via
  `ANIMAL_MGR_REMOVE` (`0x196850`, arg = `animal+0x2540`).
- `Player` → `charStats->vtbl[0x130](charStats, 0)` (`CHARSTATS_DEATH_TRIGGER_SLOT`) —
  the real death → lootstone + worn-loot trigger.
- `NPCS::PlayerBased` (`NPCDecorative`) → its own `vtbl[0x60]`, which is why a
  character-backed NPC gets a tombstone for free.

### Getting curated item loot

Because there is no drop table, curated item-loot needs **one of**:

1. a **skinning recipe** that produces the items from the carcass (the vanilla path), or
2. the **Stage-2 custom death node** — redirect the animal away from `createCorpse`
   into the **Player** death trigger so it drops a tombstone with worn loot instead of
   a carcass. The `HostileNPCs` work hooks `createCorpse` and, for animals *we* spawned
   (gated by `Hooks::AnimRemap::IsHostile`), invokes the same Player death trigger
   (`charStats->vtbl[0x130]`), then runs `createCorpse`'s manager-cleanup tail and skips
   the carcass. Real wild animals fall through untouched. See
   `source/server/hooks/character/hook_animal_death.h` and the
   `hostile-npc-ai-path-comparison.md` §3.4 divergence note. The canonical worn-loot
   mechanism is the `NPCDecorative` Player-tombstone path (issue #125 / `LifxDropWornToCorpse`),
   not the carcass.

## 5. Wild spawns — `cm_spawn_patterns.xml`, not SQL

Wild populations are driven by `data/cm_spawn_patterns.xml`, parsed by
`Animals::SpawnControl`. Families reference an `AnimalData` datablock by name and are
keyed to terrain **substances** (biomes), e.g.:

```xml
<family name="WolfData" count="2 5" quality="30 80"/>
```

`name` must resolve (RTTI-checked) to an existing `AnimalData` SimObject or the loader
rejects the entry (`No animalData with name %s`). Full element/attribute schema, the
`SpawnControl` RVAs (`_loadPatternsFromXML` `0x19C7A0`, `processTick` `0x19E310`, …),
and "spawn in a zone with an intro animation" recipes are in `ai_and_spawning.md` —
not repeated here.

## 6. Adding a new animal / humanoid type from a mod

Do **not** `INSERT` into `objects_types`. Register the type **in-engine** at boot via
the Jorvik pattern:

```cs
LiFx::registerObjectsTypes( ScriptObject( ... : ObjectsTypes ) {
    id;          // objects_types ID
    ObjectName;
    ParentID;    // e.g. 751 for a wild-animal type, 624 for a carcass
    // ...
} );
```

The engine creates/validates the row (correct parentage, no SQL surgery). The
datablock **`id`** is the small **CM_REV registry** index (distinct from the
`objects_types` ID): animals occupy **`61`–`146`**; **`147`+ is free**. New creatures
then spawn via `createAnimal` / `cm_spawn_patterns.xml` (§5) keyed by their
`objects_types` ID. This is the path the `HostileNPCs` player-model bandit uses. See
the Jorvik mod-bugs notes for the LiFx mod scaffold and the MySQL-access reference for
DB inspection.

## Status & provenance

- **Runtime-verified (direct MySQL query, 2026-06-16):** the absence of
  `animal_breeds` / `animal_drop_items` / `animal_spawn_patterns` in `lif_world_c`; the
  `objects_types` IDs (`751`, `755`, `624`, `915`, `928`) and `MAX(ID)=3016`; loot
  flowing through `recipe`/`recipe_requirement`.
- **Reverse-engineered (Ghidra; some runtime-confirmed):** all RVAs in §3–§4 are from
  `source/server/cm_offsets.h` (`CREATE_ANIMAL 0x195FD0`, `ANIMAL_GET_DATABLOCK_BY_TYPE
  0x18C790`, `ANIMAL_CREATE_CORPSE 0x18A370`, `ANIMAL_MGR_REMOVE 0x196850`, death router
  `0x3BE890`, `rawCorpseObjectTypeID @ datablock+0x8478`). The death-routing hook and the
  `+0x8478` field name are documented in `hook_animal_death.h`.
- **Inferred / not exhaustively verified:** the precise full field list of `AnimalData`
  (only `weaponData`, `weaponWeight`, `powerHit*`/`fastHit*`, `maxHP`,
  `rawCorpseObjectTypeID` are named); the exact carcass-skinning recipe rows; the
  CM_REV free-range boundary (`147`+) is an observed convention, not an enforced limit.
