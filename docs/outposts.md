---
title: Outposts
status: re
domain: reverse-engineering
tags: [outposts, claims, radius, proximity]
related: [../ghdocs/docs/outposts.md]
updated: 2026-06-26
---
# Outposts

How outpost influence radius and production are wired in LiF:YO, what is and isn't configurable from text files, and the LiFx leverage points.

## Storage (DB)

`lif_server_320850/sql/patch.sql`:

```sql
CREATE TABLE guild_lands (
    ID            INT,
    GuildID       INT,
    CenterGeoID   INT,
    Radius        INT UNSIGNED,                          -- influence radius
    LandType      INT COMMENT '3=Yo, 4=Outpost',
    ...
);

CREATE TABLE outposts (
    ID                     INT,
    UnmovableObjectID      INT,
    ProductionObjectTypeID INT NULL,                     -- what the outpost produces
    SecondaryContainerID   INT,                          -- where output accumulates
    OwnerGuildID           INT NULL,
    ...
);
```

`Radius` is an unconstrained `INT UNSIGNED` — the schema accepts any value. The constraint is purely in the binary that fills it in.

## Creation path

`p_createOutpostLandAndClaim(inGuildID, inCenterGeoID, inRadius)` is the SQL stored proc that creates the `guild_lands` row when an outpost is built. The binary is the only caller. The call site is in `Lands::DB::CreateOutpostLandAndClaim` at RVA 0x2BA8B0, which decompiles to roughly:

```c
ulong radius = FUN_140187360();                                // ← the constant
FUN_1402b8100(conn, &result,
              "call p_createOutpostLandAndClaim(%u, %u, %u);",
              guildID, centerGeoID, radius);
```

`FUN_140187360` is the entire "default outpost radius" decompile:

```c
ulonglong FUN_140187360(void) { return 0x14; }   // RVA 0x187360 — returns 20
```

That literal `0x14` is the hardcoded engine default. **Hooking this single getter changes the default radius for every newly-built outpost.** The current LiFx hook lives in `source/server/hooks/outpost/hook_outpost_default_radius.cpp`.

## Live mutation path (wired)

`Lands::Manager::changeGuildLandRadius` at RVA 0x2D0F00:

```c
ulonglong changeGuildLandRadius(Lands::Manager* this,
                                GuildLandRef    landRef,   // tagged value
                                uint            newRadius);
```

- `landRef`: 8-byte tagged handle. Low byte = land type (must be `0x01` or the function rejects with "wrong land type"). Bytes `[4..7]` = guild_land ID.
- Persists via `UPDATE guild_lands SET Radius=%u WHERE ID=%u;` and then rebuilds the monument.

From LiFx:
1. The `Lands::Manager` singleton lives at `image_base + 0xB62F68` (`LANDS_MANAGER_SINGLETON` in `cm_offsets.h`). Both `guildLandsMaintenance` (0x2C0B00) and `initialLoadGuildLandsFromDB` (0x2D6220) are reached through this slot, and the maintenance tick itself calls `changeGuildLandRadius(*singleton, ...)` so the slot is unambiguous.
2. The tagged `landRef` is `(uint64_t)landID << 32 | 0x01`.

Exposed as `Lifx::setOutpostRadius(landID, newRadius)`.

## Production

Output is two-layer:

| Layer | Where | How to change |
|---|---|---|
| **What** an outpost produces | `outposts.ProductionObjectTypeID` (DB) | Direct DB write, or hook `_onOutpostObjectCompleted` to set it on creation. |
| **How much** an outpost produces per tick | Computed in-binary from `(baseItemRate, outpostQuality, slaveCount, slaveQuality)` per cm_messages.xml string id 4852 | Hook the production-tick path reachable from `OutpostEvent` / `OutpostBunny`. RVAs not yet resolved. |

cm_messages.xml:4852 (verbatim):

> "Outpost production rate depends on base item type production rate, quality of outpost, quality and amount of dead and alive slaves"

## Useful RVAs

| Symbol | RVA | Notes |
|---|---|---|
| `Lands::Manager::changeGuildLandRadius` | `0x2D0F00` | DB UPDATE + rebuild monument. Needs `Lands::Manager*`. |
| `Lands::DB::CreateOutpostLandAndClaim` | `0x2BA8B0` | Caller of `p_createOutpostLandAndClaim`. Calls the getter below. |
| **Default outpost radius getter** | `0x187360` | `return 20;`. **Hooked.** Backed by `Hooks::Outpost::g_defaultRadius`. |
| `Lands::Manager::canBuildOutpostAt` | `0x2D02D0` | Placement validation. |
| `Lands::Manager::checkPersonalLandOutpostDistance` | `0x2D1A70` | Min distance to personal land. |
| `Lands::Manager::createGuildLand` | `0x2D2170` | Higher-level guild-land factory. |
| `Lands::Manager::guildLandsMaintenance` | `0x2C0B00` | Periodic tick — also useful for singleton discovery. |
| `Lands::Manager::initialLoadGuildLandsFromDB` | `0x2D6220` | Boot-time loader. |
| `Lands::Config::GetGuildLandConfig` | `0x2B1D70` | Monument-level → settlement radius lookup (table at `DAT_140ad1844`, stride 0x1C). |
| `Guilds::Changes::ChangeGuildLandOuterRadius::apply` | `0x2B3F10` | High-level command that flows into `changeGuildLandRadius`. |
| `OutpostClaimDurationMin` registration | `0x156940` | Already a TorqueScript pref. |

## LiFx commands

```
Lifx::getOutpostDefaultRadius()                            -> int   current default for new outposts (engine baseline: 20)
Lifx::setOutpostDefaultRadius(radius)                      -> void  override; takes effect on next outpost built
Lifx::setOutpostRadius(landID, radius)                     -> void  change an EXISTING outpost's radius (engine persists + rebuilds)
Lifx::setOutpostProductionType(unmovableID, type)          -> void  retarget existing outpost output (engine writes outposts.ProductionObjectTypeID)
Lifx::dumpOutposts()                                       -> void  list every live outpost (UnmovableObjectID, outpostType, perTickCount, ProductionObjectTypeID, currentQuality)
Lifx::setMonumentMinDistance(d)                            -> void  guild monument ↔ guild monument min distance (baseline 150)
Lifx::setOutpostOutpostMinDistance(d)                      -> void  outpost ↔ outpost/guild-land min distance (baseline 300)
Lifx::setOutpostMinDistanceToPersonalClaim(d)              -> void  outpost ↔ personal claim min distance (baseline 20, independent of default radius)
Lifx::setMonumentMinDistanceToPersonalClaim(d)             -> void  guild monument ↔ personal claim min distance (baseline 20)
```

## Proximity knobs

Two distinct mechanisms back the four proximity setters above:

1. **Constant-returner detours** for `setMonumentMinDistance` (RVA `0x2B1EF0` = `return 150`) and `setOutpostOutpostMinDistance` (RVA `0x2B1F00` = `return 300`). Each engine helper is one instruction and is referenced from exactly the right callers, so a standard Detours attach is enough.

2. **Call-site rel32 retargets** for the two personal-claim setters. The underlying constant in both cases is `FUN_140187360` (RVA `0x187360`, `return 20`) — the same getter as `setOutpostDefaultRadius`. Globally redirecting it would also move the default radius. Instead, we byte-patch the `rel32` of two specific `E8 …` call instructions so each invokes a wrapper inside this DLL:

   | Call site RVA | Original `E8 rel32` bytes | Inside | Now calls |
   |---|---|---|---|
   | `0x2D1C65` | `E8 F6 56 EB FF` | `Lands::Manager::checkPersonalLandOutpostDistance` (`0x2D1A70`) | `Hooks::Outpost::OutpostToPersonalDistanceWrapper` |
   | `0x2CFE45` | `E8 16 75 EB FF` | `Lands::Manager::canBuildMonumentAt` (`0x2CF9D0`) | `Hooks::Outpost::MonumentToPersonalDistanceWrapper` |

   The other two call sites to `0x187360` inside `canBuildOutpostAt` (`0x2D0375` footprint, `0x2D0853` vs guild-yo) are **intentionally left untouched** so they keep tracking `setOutpostDefaultRadius`.

   Validation is loud: if either site doesn't start with `0xE8`, or the wrapper ends up >2GB from the call site (it shouldn't on any sane DLL load), `PatchPersonalClaimCallSites` logs a warning and aborts that one patch.

### Failure messages

| Gate | Message id | Text |
|---|---|---|
| Terrain unbuildable (msg 756 callers) | 756 | "God does not allow you to claim these lands!" |
| Outpost too close to personal claim | 748 | (varies) |
| Personal claims overlap | 754 | (varies) |

Message 756 specifically is a **terrain** check (`canBuildPersonalMonumentAt` at `0x2D0B30` / `canClaimPersonalLandAt` at `0x2D0DC0` / `canBuildMonumentAt` / `canBuildOutpostAt`), not a proximity check — no proximity tweak relaxes it.

## Singleton & helper RVAs

| Symbol | RVA | Notes |
|---|---|---|
| `Lands::Manager*` slot | `0xB62F68` | Read once at call-time, no caching. |
| `Outposts::Manager*` slot | `0xB5AD68` | Same shape — slot populated during engine init. |
| `Outposts::Manager::_findObject` | `0x2ED0C0` | Map lookup keyed by ComplexObjectID. Currently unused — we iterate the list instead. |
| `Outposts::Outpost::setProductionType` | `0x2E8A10` | Writes `outpost+0x14` and emits the `UPDATE outposts SET ProductionObjectTypeID=…` SQL itself. |

`Outposts::Manager` in-memory store: `+0x18` `map<ComplexObjectID, Outpost**>`, `+0x20` `std::list<Outpost*>` sentinel head, `+0x28` `size`. Per-`Outpost` (corrected from full reverse of `Outposts::Outpost::maintenance` + `recalcSlaves`):

| offset | field |
|---|---|
| `+0x00` | UnmovableObjectID (u32) |
| `+0x08` | outpostType (u32, 1702..1711 — key into `DAT_140AD20B0`) |
| `+0x10` | perTickCount (u32, written by `recalcSlaves`) |
| `+0x14` | ProductionObjectTypeID (u32, what `setProductionType` writes) |
| `+0x18` | currentQuality (u32-widened u16) |
| `+0x1C` | damagedFlag (u8) |
| `+0x28..+0x30` | `std::vector<SlaveSlot>` (stride 0x28) |
| `+0x88/+0x90` | secondary inventory ptr + shared_ptr refcount |

OwnerGuildID is **not** on the in-memory Outpost — it lives in `guild_lands.GuildID` keyed by UnmovableObjectID.

## Follow-ups

1. **Production multiplier.** Entry point is `Outposts::Outpost::maintenance` at RVA `0x2E8F00`, which inlines the per-tick `produce 1 item` path (literal `1` immediate passed to `FUN_140279cf0`). Two viable shapes: (a) byte-patch the immediate, (b) trampoline `maintenance` and re-drive the insertion N-1 more times against the same transaction. (b) is cleaner but needs more reversing of the secondary-inventory transaction lifecycle. Punted.
2. **Hook `_onOutpostObjectCompleted` (RVA `0x2EDC90`)** to set production type at creation time, so `setOutpostProductionType` becomes redundant for new outposts. Not strictly needed — the runtime setter above is sufficient — but a nicer ergonomics win if we later add a config file.
