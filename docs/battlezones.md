---
title: Battlezones (starting-zone land)
status: re
domain: reverse-engineering
tags: [lands, battlezone, containment, lifx-api, torquescript, movement]
related: [net_events.md, outposts.md]
sources: [source/server/cm_offsets.h, source/server/api/lifx_battlezone.cpp, source/server/hooks/battlezone/hook_battlezone_containment.cpp, dist/mods/Battlezones/battlezones.cs, docs/outposts.md]
updated: 2026-06-26
---

# Battlezones (starting zones) & land-boundary containment

What a "battlezone" actually is in LiF:YO, how it's created/stored/deleted, and — the part the community cares about — what happens to a player who walks into one. All RVAs verified against the current `ddctd_cm_yo_server.exe` (image base `0x140000000`; RVA = VA − base). Engine source paths in the binary point at `engine\source\app\lands\landsmanager.cpp` and `engine\source\t3d\player.cpp`.

> **TL;DR on the "you're locked in forever" rumor.** Partly true, badly worded. A battlezone is a real engine land type (`Lands::BattleZoneLand`). When its **active flag** is set and its **subtype == 1** ("active starting zone"), the per-step movement handler teleports a player who tries to step out **back to their previous tile**. It is *not* permanent — it's gated on a per-zone active byte (`+0x34`); clear it and the player walks out. It is a snap-back at the boundary, not an inescapable trap. (Msg 4761 "Return to fight!" is thematically related but **never referenced in the binary** — see the note in the containment section.)

## What a battlezone is

`Lands::BattleZoneLand` is one of four land kinds under the `Lands::ILand` interface, all managed by the `Lands::Manager` singleton:

```
Lands::ILand  (7-slot vtable)
 ├─ GuildLand      (handle tag 1)
 ├─ PersonalLand   (handle tag 2/3)
 ├─ AdminLand      (handle tag 2/3)
 └─ BattleZoneLand (handle tag 4)   ← "battlezone" / "starting zone"
```

The land-kind tag (1..4) is the low byte of a land handle (same tagged-handle scheme as `outposts.md`'s `landRef`). `getAllLandsOfType` (`0x2D4BA0`) validates `type-1 < 4` and indexes per-type containers at `manager + type*0x40 + 0x1F0` (list head) / `+0x1F8` (count).

A battlezone object is **0x38 bytes**, vtable `Lands::BattleZoneLand::vftable` @ RVA `0x7D4060`. Its slots `[2..6]` are byte-identical to `AdminLand` — i.e. a battlezone is modeled as an **admin/server-defined region with no per-subject claim rules**, not a guild claim.

| offset | field | accessor |
|---|---|---|
| `+0x00` | vtable ptr | — |
| `+0x18` | geoId + radius (set by `FUN_140278420`) | radius `FUN_1402B1C80`, geoId `FUN_1402B1C70` |
| `+0x20`/`+0x28` | refcounted name/owner handle | — |
| `+0x30` | **subtype** (u32, the `type` arg to `createBattleZone`) | `FUN_1400AB920` |
| `+0x34` | **active flag** (u8, ctor sets `1`) | `FUN_1402A1A80` |
| `+0x04` (via base) | land Id | `FUN_1402B2BB0`→`+4` |

## The TS-exposed API

These three engine functions are **real and present in the binary** (registered as TorqueScript console functions). The signature matches the LiFx stub exactly — the unnamed third int is `radius`:

```
createBattleZone(U32 type, U32 geoIdInt, U32 radius, const char* name)
deleteBattleZone(U32 landDbId)
printBattleZones()
```

| Symbol | RVA | Notes |
|---|---|---|
| `createBattleZone` console-register thunk | `0x361C0` | registers name + handler `0x2D45C0` |
| `createBattleZone` engine-init-checked handler | `0x2D45C0` | "Engine not initialized…" guard, then calls `createBattleZoneLand` |
| second `createBattleZone` TS entry (strtoul args) | `0x2CB7A0` | parses up to 4 string args, same target |
| `Lands::Manager::createBattleZoneLand` | `0x2CB8A0` | `landsmanager.cpp:0x5D9`. `operator new(0x38)` → ctor `0x2A17A0`; stores in map @ `singleton+0x2E8`, list @ `singleton+0x2F0`; posts `Guilds::Changes::CreateBattleZoneLandChange` (ctor `0x2B4920`) for client sync |
| `BattleZoneLand` ctor | `0x2A17A0` | sets vtable, `+0x30`=type, `+0x34`=1 |
| `deleteBattleZone` thunk / handler | `0x362C0` / `0x2D4700` | handler → `deleteBattleZoneLand` `0x2CBAB0` |
| `Lands::Manager::deleteBattleZoneLand` | `0x2CBAB0` | builds a delete-change and posts via `0x2D9750` |
| `printBattleZones` handler / impl | `0x2D47D0` / `0x2D8920` | impl `landsmanager.cpp:0x1184`; logs `"  %u - GeoID = %u, Radius = %u, Type = %u"` over the list @ `singleton+0x2F0` |
| `Lands::Manager` singleton slot | `0xB62F68` | same slot as `outposts.md` |

There is **no dedicated DB table** for battlezones in the load path (the boot loader `0x2D6220`, named `initialLoadGuildLandsFromDB` in `cm_offsets.h`, loads `guild_lands`/`admin_lands`/`claims` only). Battlezones are created at runtime via the `createBattleZone` console function (e.g. from a server script or event), and synced to clients through the change object — they are not persisted like guild/personal/admin lands.

### Named constants in `cm_offsets.h`

The battlezone block of `source/server/cm_offsets.h` (the `// ---- Battlezones (Lands::BattleZoneLand, handle tag 4) ----` section) gives canonical names to the RVAs the LiFx build links against. Cite these names in code, not bare hex:

| RVA | `CmOffset` constant | Meaning |
|---|---|---|
| `0x2CB8A0` | `CREATE_BATTLEZONE_LAND` | `Lands::Manager::createBattleZoneLand(type, geoIdInt, radius, name)` |
| `0x2CBAB0` | `DELETE_BATTLEZONE_LAND` | `Lands::Manager::deleteBattleZoneLand(landDbId)` |
| `0x2D78C0` | `ISACTIVE_STARTING_ZONE` | `Lands::Manager::isActiveStartingZone(landHandle) -> bool` — the containment gate |
| `0x0F0500` | `PLAYER_CHECK_STEPS` | `Player::_checkSteps(Player*)` — per-step movement handler |
| `0x2B2BB0` | `BATTLEZONE_LAND_GET_ID` | id `= *(u32*)(GetIdHolder(land)+4)` |
| `0x2B1C70` | `BATTLEZONE_LAND_GET_GEOID` | geoId `= *(u32*)GetGeoId(land+0x18, &out)` |
| `0x2B1C80` | `BATTLEZONE_LAND_GET_RADIUS` | radius `= GetRadius(land+0x18)` |
| `0xB62F68` | `LANDS_MANAGER_SINGLETON` | `*(Lands::Manager**)(base + 0xB62F68)` (shared with `outposts.md`) |

`cm_offsets.h` also records the direct field reads used by `printBattleZones`: `type = *(u32*)(land+0x30)`, `active = *(u8*)(land+0x34)`. The remaining RVAs in this page (vtable `0x7D4060`, the TS thunks/handlers, ctor `0x2A17A0`, the change-object ctors, the `Player`/claim helpers) are **not** named constants — they are decompile-only references and are cited by raw RVA.

## Battlezone predicates

| Symbol | RVA | Behaviour |
|---|---|---|
| `Lands::Manager::isActiveStartingZone` | `0x2D78C0` | asserts handle **tag == 4** ("wrong land" otherwise, `:0x111F`). Returns **true iff** `subtype(+0x30) == 1` **and** `activeFlag(+0x34) != 0`. This is the gate for boundary containment. |
| `Lands::Manager::isLandProtectFromRaiding` | `0x2D79A0` | tag **!= 1** → returns **true unconditionally** (battlezones, admin & personal lands are always raid-protected). tag == 1 (guild) runs the offline-raid-protection + guild-standing logic (standing codes `0x98..0x9B`). |
| `Lands::Manager::getAllLandsOfType` | `0x2D4BA0` | enumerate by tag 1..4. |

## Player-side containment (the "lock-in")

`Player::_checkSteps` (`0xF0500`, `player.cpp:0x3992`) runs on each movement step. The player object caches its current geoId and one membership handle per land kind:

| Player offset | holds |
|---|---|
| `+0x2464` | current geoId |
| `+0x248C` | GuildLand membership |
| `+0x2494` | PersonalLand membership |
| `+0x24A4` | AdminLand membership |
| `+0x24AC` | **BattleZoneLand / starting-zone membership** |

When the geoId changes, `_checkSteps`:

1. **Active-starting-zone snap-back** — if the player is in an active starting zone (`isActiveStartingZone(+0x24AC)` true) and the step would change that membership, it calls **`Player::teleportTo(player, oldGeoId@+0x2464)`** (`0x102D60`, `player.cpp:0x339D`) and returns early — the player is put back on the tile they came from. *This is the battlezone lock-in.* (The snap-back happens in the move path with no message-send at this site; msg 4761 "Return to fight!" looks thematically related but is **never referenced anywhere in the binary**, so it is not confirmed as this branch's UI string — see the prep-clamp note below. The earlier draft asserting msg 4761 is sent here was **retracted**.)
2. **Claim trespass** — `FUN_1400F0170` (only active when server mode `FUN_1401556E0() == 2`): if the destination land's claim denies the player `CanEnter`, it emits **msg 0x129A = 4762 "You have no rights to enter territory of %1 town"** and teleports the player out via `FUN_140102F30` to either the old geoId or `getRandomPosOutsideOfGuildLand` (`0x2D59F0`). This keeps non-members *out* of claims (the inverse boundary).
3. **Land-enter triggers** — fires `CharacterTriggers::{GuildLand,PersonalLand,AdminLand}TriggerSignal` (these drive the `enter*Land` signals in `data/characterTriggers.xml`). Note there is **no** `BattleZoneLandTriggerSignal` — battlezones use the snap-back branch instead.

Access-check helper: `FUN_1402D1360` walks the lands at a geoId, reads the claim-rules bitfield (`FUN_1402AA140`) and tests permission bit `param_4` (CanEnter/CanBuild/…).

## Verdict on the rumor

- **True:** entering an *active starting-zone* battlezone does confine you — stepping across its boundary teleports you back to your previous tile.
- **False ("forever"):** containment is gated on the per-zone active byte `+0x34` and `subtype == 1`. It is not a permanent or character-state lock; clearing the flag (or a battlezone of a different subtype) lets the player leave normally. It's a boundary snap-back, not a trap.
- **Separate mechanism — traced, and absent in YO:** the Battle-Totem PvP-event *preparation clamp* ("cannot move further than 50 m from the spawn point", msg 2868/2871) is distance-from-spawn, not zone-boundary. See the next section — it is **not implemented in the YO server**.

## Battle-Totem arena prep-clamp: not implemented in YO

The community "battle" mechanics (preparation-phase movement clamp, central-area capture, deserter penalty, "5 minutes to leave the battlefield") have message strings in `data/cm_messages.xml` but **no implementation in the YO server**. Evidence (constant-xref scan via `LifxConstXrefs.java` over the whole binary, plus a `.dso` scan of the server scripts):

| Msg | id (hex) | Only occurrence in binary | Verdict |
|---|---|---|---|
| "…can't move further than 50 m from the spawn point…" | 2868 / `0xB34` | `cmskillability.cpp:0xB34` line number in `Ability::_parseXmlEntityNode` | not a message-send |
| full battle ruleset | 2871 / `0xB37` | `cmskillability.cpp:0xB37` line number (same function) | not a message-send |
| "5 minutes to leave the battlefield" | 4568 / `0x11D8` | `landsmanager.cpp:0x11D8` line number in `getAdminLandByName` (coincidence) | not a message-send |
| deserter prompt | 727 / `0x2D7` | `cmmessages.cpp:0x2D7` line number in `CmMessage::processTags` | not a message-send |
| "Return to fight!" | 4761 / `0x1299` | **0 references anywhere** | unused |

For contrast, message IDs that *are* sent go through the CmMessage-by-id builder `FUN_140169760(buf, id)` as a real immediate — e.g. the verified trespass message `FUN_140169760(buf, 0x129A)` (4762) in `FUN_1400F0170`. None of the battle IDs above appear in that builder, and there is no battle/pvp/totem `.cs`/`.cs.dso` in `lif_server_320850_d/` (only the `battleaxe` weapon datablock matches "battle").

**Conclusion:** the Battle-Totem arena (with its prep-clamp) is a **LiF:MMO feature**; its strings are inherited in YO's shared message table but the logic was never wired into the YO server. There is **no RVA for the prep-clamp in this binary** — the only real, YO-implemented zone confinement is the `BattleZoneLand` active-starting-zone snap-back documented above. The Battle Totem object (`cm_objects.xml` ids ~17691/20263, recipe 11738) is craftable but has no associated arena handler in the server.

## Hookable leverage (candidates)

- `isActiveStartingZone` (`0x2D78C0`, `ISACTIVE_STARTING_ZONE`) — flip the gate (return constant) to globally enable/disable battlezone containment without touching individual zones' `+0x34`.
- `BattleZoneLand` ctor `+0x34` / `+0x30` — set subtype & active flag at creation.
- `createBattleZoneLand` (`0x2CB8A0`, `CREATE_BATTLEZONE_LAND`) / `deleteBattleZoneLand` (`0x2CBAB0`, `DELETE_BATTLEZONE_LAND`) — already script-reachable via `createBattleZone`/`deleteBattleZone`; a LiFx wrapper could expose them as `Lifx::createBattleZone(...)`.

## LiFx API

LiFx wraps the engine primitive into a usable, scriptable surface (module `source/server/api/lifx_battlezone.cpp`; containment hook `source/server/hooks/battlezone/hook_battlezone_containment.cpp`):

| Command | Signature | Notes |
|---|---|---|
| `Lifx::createBattleZone` | `(int type, int geoIdInt, int radius, string name) -> int landId` | Calls the engine TS `createBattleZone`, then reads the new zone's id back from the list at `singleton+0x2F0` (engine create returns void). `type==1` + active confines players. Returns -1 on failure. |
| `Lifx::deleteBattleZone` | `(int landId) -> void` | Removes a zone. |
| `Lifx::printBattleZones` | `() -> void` | Engine passthrough (logs to console). |
| `Lifx::getBattleZones` | `() -> string` | Newline rows `id geo radius type active` — the parseable counterpart to `printBattleZones`. |
| `Lifx::setBattleZoneActive` | `(int landId, bool active) -> void` | **Arm/disarm containment live** by writing `+0x34` (and forcing subtype `+0x30=1` when arming). No delete+recreate. |
| `Lifx::setBattleZoneExempt` | `(int charId, bool exempt) -> void` | Let a charId pass an armed boundary. |

**Containment hook.** `isActiveStartingZone` has no player argument, so per-player exemption is done with two cooperating detours: `Player::_checkSteps` (`0xF0500`, `PLAYER_CHECK_STEPS`) stamps the current `Player*`; the `isActiveStartingZone` (`0x2D78C0`, `ISACTIVE_STARTING_ZONE`) detour reads that player's charId (`+0x1B44`), returns "not active" for exempt charIds, and otherwise fires a throttled TorqueScript callback `LifxBattleZoneOnContained(charId)` so the script can show msg 4761 "Return to fight!". With no exemptions set and the callback absent, behaviour is identical to stock.

**Re-seeder + arena controller** (`dist/mods/Battlezones/battlezones.cs`, deploy to `<server>/mods/Battlezones/`): battlezones aren't DB-persisted, so `LifxBattleZoneSeedAll()` (run on `onServerCreated`) recreates a configured set each boot. `$LiFx::battlezone[i, …]` config keys: `geoId/radius/name/type/active`. The minimal arena controller (`lifxArenaStart`/`lifxArenaEnd`) arms a zone, optionally teleports players in, and tears down after a timer; capture/scoring/teleport/message glue are marked integration points (server-build specific).

## Tooling

Generated with the repo Ghidra scripts: `LifxExport.java` (strings/functions/classes), plus three added here — `LifxDecompileArgs.java` (decompile by RVA args), `LifxVtableDump.java` (dump vtables by symbol/RVA), `LifxXrefs.java` (callers of an RVA). Re-run via `analyzeHeadless ~/ghidra_projects LiF -process ddctd_cm_yo_server.exe -noanalysis -scriptPath scripts/ghidra -postScript <script>.java <args>`.

## Status & provenance

- **Runtime-verified:** nothing on this page has been confirmed live on a running server yet. Treat the RE as static-analysis-grade.
- **Statically verified (decompile / vtable / xref):** the land-kind taxonomy and tag scheme; `BattleZoneLand` layout (`0x38` bytes, vtable `0x7D4060`, fields `+0x30` subtype / `+0x34` active); the TS-function reality of `createBattleZone`/`deleteBattleZone`/`printBattleZones` and their handler chain; the `isActiveStartingZone` gate (`tag==4 && +0x30==1 && +0x34!=0`); the `_checkSteps` snap-back via `teleportTo` and the claim-trespass branch (msg `0x129A`/4762); the negative result that the Battle-Totem prep-clamp (msg 2868/2871) and msg 4761 are **unimplemented / unreferenced** in the YO binary. All RVAs checked against `ddctd_cm_yo_server.exe` (base `0x140000000`).
- **`cm_offsets.h` cross-check:** every RVA on this page that has a named `CmOffset` constant matches (`CREATE_BATTLEZONE_LAND` `0x2CB8A0`, `DELETE_BATTLEZONE_LAND` `0x2CBAB0`, `ISACTIVE_STARTING_ZONE` `0x2D78C0`, `PLAYER_CHECK_STEPS` `0x0F0500`, `BATTLEZONE_LAND_GET_ID` `0x2B2BB0`, `BATTLEZONE_LAND_GET_GEOID` `0x2B1C70`, `BATTLEZONE_LAND_GET_RADIUS` `0x2B1C80`, `LANDS_MANAGER_SINGLETON` `0xB62F68`). No conflicts found.
- **Built, not run:** the LiFx API (`lifx_battlezone.cpp`) and containment hook (`hook_battlezone_containment.cpp`) compile clean under clang-cl/xwin but are **NOT runtime-tested**. The pointer walks they depend on — list `@ singleton+0x2F0`, the id/geo/radius accessors, and the `+0x30`/`+0x34` field writes — still need an on-server smoke test before they can be trusted.
- **Change history:** the RE write-up landed in PR #138 (closes issue #137). The LiFx API + re-seeder/arena mod landed in PR #141 (closes #140, stacked on #138). The retraction of the unverified "msg 4761 is sent from `_checkSteps`" claim was part of #141.
