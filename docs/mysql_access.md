---
title: LiF MySQL access
status: reference
domain: operations
tags: [mysql, mariadb, database, schema, terrain-blocks, ops]
related: [outposts.md, sector_handoff_design.md, reverse_engineering.md]
sources: [source/server/cm_offsets.h, outposts.md, sector_handoff_design.md]
updated: 2026-06-26
---

# LiF MySQL access

The LiF:YO server keeps all world state in MariaDB. Each shard owns a `lif_X` database; the live schema has drifted from the seed dump, so always `DESCRIBE` before composing writes. This page is the canonical record of how to reach the DB, the write-safety guard, the known schema drift, and the engine-side functions that read/write those tables at runtime.

## Connection

MariaDB runs on **`127.0.0.1:3306`**.

| Field | Value |
|---|---|
| host | `127.0.0.1` |
| port | `3306` |
| user | `NEWROOT` |
| pass | `NEWPASS` |

Invocation (use only when a query is explicitly authorized):

```
mysql -h 127.0.0.1 -P 3306 -u NEWROOT -pNEWPASS lif_1 -e "<query>"
```

**Socket gotcha:** do **not** rely on the `mysql` default socket. The user's `.my.cnf` points at `/home/container/run/mysqld/mysqld.sock`, which is a *container-internal* path that does not exist on the host. Bare `mysql` will fail or connect to the wrong place. Always pass `-h 127.0.0.1 -P 3306` explicitly.

## Databases / shards

Each shard is a separate `lif_X` database (e.g. `lif_1`, `lif_2`, and per-world DBs such as `lif_world_c`). `lif_1` / `lif_2` are the shared shards.

### Write-safety guard

In **auto mode** the classifier blocks MySQL writes against the shared `lif_1` / `lif_2` databases. Out of auto mode writes are allowed, but you should still ask before running destructive statements — `DROP`, `TRUNCATE`, or any `UPDATE`/`DELETE` without a `WHERE`.

## Schema drift

The seed dump at `lif_server_320850/sql/new.sql` is **older than the live schema**. The known divergence:

- `terrain_blocks` in the live DB has a **`NOT NULL RegionID`** column that is **absent from the seed**. An `INSERT` modeled on the seed will fail.

Rule: run `DESCRIBE <table>` against the live DB before composing any `INSERT`.

### `terrain_blocks` RegionID mapping

Shard A's default block→region mapping (also used verbatim for shard B's `451..459` mirror blocks):

| Block IDs | RegionID |
|---|---|
| `442` / `443` / `444` / `447` / `450` | `12` |
| `445` / `446` | `13` |
| `448` / `449` | `14` |

## Engine-side DB access

The server reaches the same MariaDB through its own async DB layer; these RVAs are the in-binary entry points (all are named constants in `source/server/cm_offsets.h`). Useful when a mod must touch a table the way the engine does, or to find which SQL a given table change corresponds to.

| Symbol | RVA | What it does |
|---|---|---|
| `DB_GET_WORLD_CONN` | `0x54CAF0` | `void* __fastcall(U32 idx)` — returns `(&DAT_140BF86F0)[idx]`; `idx 1` = the world-DB connection object; `idx>4` → null. World-DB connection array base is `DAT_140BF86F0`. Verified 2026-06-25. |
| `DB_EXEC_FORMATTED` | `0x0884F0` | `U8 __fastcall(void* conn, const char* fmt, const U32* a, const U32* b)` — `snprintf(buf, n, fmt, *a, *b)` then `conn->vtbl[0](conn, buf)` to run it. **Always** derefs `a`/`b`, so pass two valid `U32*` even when `fmt` has no specifiers (pre-format the SQL yourself; keep `%` out of it). |
| `CHARACTER_LOAD_INMEM` | `0x1BB290` | `CharacterLoad(cci)` — `CharacterParameters::loadFromDb` (stats + RootContainerID/EquipmentContainerID) + inventory/equipment loadFromDb. Connection-FREE. Verified 2026-06-25. |
| `EQUIP_LOAD_FROM_DB` | `0x1F2760` | `CmPlayerEquipment::loadFromDb(equip)` — populates slots from SQL `equipment_slots`. |
| `EQUIP_SET_SLOT_DB` | `0x1EEA40` | Persists a single slot to SQL `equipment_slots`. |
| `CONTAINER_TRYINIT` | `0x299140` | `CmServerInventoryContainer::tryInit` — `SELECT ... FROM items WHERE ContainerID=mID`, builds each item via `createItemFromDbResult`. Guarded by the init flag at `+0x14` (loads once, caches). |

The two raw primitives (`DB_GET_WORLD_CONN` + `DB_EXEC_FORMATTED`) were recovered from `CharacterParameters::Zed_is_dead` (RVA `0x89660`), which uses them for the `chars_deathlog` `INSERT`. `Lifx::dropBanditLoot` reuses them to move a connection-less bandit's items.

Outpost/land writes go through a stored proc rather than these primitives — see [`outposts.md`](outposts.md) (`Lands::DB::CreateOutpostLandAndClaim` at RVA `0x2BA8B0` calls `p_createOutpostLandAndClaim`; `Outposts::Outpost::setProductionType` at `0x2E8A10` emits its own `UPDATE outposts ...`). Cross-shard `movable_objects` rows (globally addressable by OID) underpin the sector-handoff design in [`sector_handoff_design.md`](sector_handoff_design.md).

## Status & provenance

- **Runtime-verified:** the connection parameters (`127.0.0.1:3306`, `NEWROOT`/`NEWPASS`) and the socket-path gotcha are operational facts confirmed in use. `DB_GET_WORLD_CONN` (idx 1 = world conn) and `CHARACTER_LOAD_INMEM` being connection-free were verified at runtime 2026-06-25.
- **Observed schema drift:** the live `terrain_blocks` `NOT NULL RegionID` column and the block→region mapping are observed against the running DB; the seed at `lif_server_320850/sql/new.sql` is stale. Re-check with `DESCRIBE` — schema may drift further.
- **Reverse-engineered (not all runtime-verified):** the engine-side DB RVAs and table associations are read from decompilation and `source/server/cm_offsets.h`; the SQL strings noted (`chars_deathlog`, `equipment_slots`, `items`, `outposts`) are the queries those functions issue.
