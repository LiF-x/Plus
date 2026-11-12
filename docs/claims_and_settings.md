---
title: Ordinary claims and settings
author: Zbigi
assistance: ChatGPT (Codex)
status: re
domain: reverse-engineering
tags: [claims, permissions, guilds, outposts, configuration, maintenance]
related: [outposts.md]
sources: [docs/evidence/claims_and_settings_reference.json, source/server/api/lifx_outpost.cpp, source/server/hooks/outpost/hook_outpost_default_radius.cpp, source/server/hooks/outpost/hook_outpost_proximity.cpp, source/server/cm_offsets.h]
updated: 2026-11-12
---

# Ordinary claims and settings

Personal claims, YO guild claims and outposts combine a **land record** (owner
and geometry) with a **claim record** (permission identity). Changing a size
default, resizing an existing land, and changing access rights are separate
operations. Use the appropriate land ID and engine operation; a database row
or console echo alone does not establish the current in-game boundary.

These documentation updates were generated with help from ChatGPT (Codex),
in collaboration with Zbigi, from notes and analyses collected over four months
of work on LiFXXL, a community project based on a modified LiF:YO server.

## Records and ID namespaces

| Record | Geometry / ownership | Permission link |
| --- | --- | --- |
| `personal_lands` | `CharID`, rectangular corners `GeoID1` / `GeoID2`, `IsTemp` | `claims.PersonalLandID` |
| `guild_lands`, `LandType=3` | YO guild land: `GuildID`, `CenterGeoID`, stored `Radius` | `claims.GuildLandID` |
| `guild_lands`, `LandType=4` | Outpost land: `GuildID`, `CenterGeoID`, stored `Radius` | `claims.GuildLandID` |
| `claims` | Claim identity, linked to a land | `claim_rules.ClaimID` |
| `claim_subjects` | A character, guild role, guild, or standing category | `claim_rules.ClaimSubjectID` |

`claims.ID`, `guild_lands.ID`, `personal_lands.ID`, a character ID and an
`UnmovableObjectID` are different namespaces. Equal numbers do not make them
interchangeable. In particular:

- `Lifx::setOutpostRadius(guildLandID, newRadius)` takes **`guild_lands.ID`**.
- `Lifx::setOutpostProductionType(unmovableObjectID, typeID)` takes the
  **building ID**, followed by an **ObjectTypeID**.
- Permission queries start from **`claims.ID`**.

The reference SQL contains historical table definitions followed by migrations.
Its early `claims.SupportPoints` column is later removed, with support points
stored on `guild_lands` and `personal_lands`. Likewise, the early land-type
comment is replaced by `3=Yo, 4=Outpost`. Inspect the installed schema rather
than copying the first `CREATE TABLE` block. Curated excerpts, including those
migrations, are in the [evidence file](evidence/claims_and_settings_reference.json).

## Access rules and creation defaults

`claim_rules` holds five separate flags:
`CanEnter`, `CanBuild`, `CanClaim`, `CanUse`, `CanDestroy`.
The subject table provides nullable `CharID`, `GuildRoleID`, `GuildID` and
`StandingTypeID` selectors. Its individual unique indexes are not a constraint
requiring exactly one selector; use the game's normal permission-management
path to construct rules.

The reference creation procedures insert these defaults. Bit strings below
follow **Enter / Build / Claim / Use / Destroy**, with `1` meaning allowed in
that row:

| Subject | YO guild claim | Outpost claim |
| --- | --- | --- |
| Leader, role `1` | `11111` | `11111` |
| Minor leader, role `2` | `11111` | `11010` |
| Member roles `3`–`6` | `11111` | `11000` |
| Recruit, role `7` | `11110` | `10000` |
| Ally, standing `5` | `10000` | `10000` |

`p_createPersonalLandAndClaim_impl` gives the owning character a `11111`
rule. These are **creation defaults**, not a claim about today's saved rules,
conflict precedence between multiple matching subjects, or every ability's
additional checks. For a reported access failure, inspect the actual subject
and rule before changing geometry or placement distances.

Individual buildings also have `unmovable_objects_claims` and
`claim_rules_unmovable`. The latter exposes `CanUse` and `CanDestroy`, not all
five land flags. Check these rows when a single building behaves differently
from the surrounding land; the presence of a row alone does not explain how
every ability resolves competing rules.

## Guild settings: reference values versus running values

The reference `scripts/server/cm_config.cs` contains:

| Preference suffix under `$cm_config::Claims::` | Reference value |
| --- | --- |
| `guildLevel1RadiusTown` / `guildLevel1RadiusCountry` | `20` / `20` |
| `guildLevel2RadiusTown` / `guildLevel2RadiusCountry` | `30` / `30` |
| `guildLevel3RadiusTown` / `guildLevel3RadiusCountry` | `50` / `50` |
| `guildLevel4RadiusTown` / `guildLevel4RadiusCountry` | `100` / `100` |
| `guildLevel1MinSacrificeBasePrice` … `guildLevel4MinSacrificeBasePrice` | `1`, `10`, `50`, `100` |
| `GuildCellPrice` | `1` |
| `MonumentGrowth` | `0.05` |

These are values read from the reference script, **not current LiFXXL server
settings**. The native guild-level lookup at RVA `0x2B1D70` returns a size
field from a table with stride `0x1C`; its on-disk level `1`–`4` entries are
also `20`, `30`, `50`, `100`. It returns a scalar, not a configuration pointer.

To inspect an already-running server's script variables:

```tcl
echo($cm_config::Claims::guildLevel1RadiusTown);
echo($cm_config::Claims::guildLevel1RadiusCountry);
echo($cm_config::Claims::GuildCellPrice);
echo($cm_config::Claims::MonumentGrowth);
```

For persistent changes, edit the deployment's actual loaded configuration or
startup override, after locating where that deployment loads these settings.
A `dev/sourceServer/` reference copy is not automatically a runtime input.
A changed script value also does not prove that an already-created engine
configuration or existing land has been rebuilt. Verify the loaded preference,
the saved land, and the visible/usable boundary at the appropriate lifecycle
step. This evidence does not establish a universal hot-reload command for all
guild preferences.

### The legacy `Radius` name needs care

Do not convert all the numbers above into literal circle radii in metres.
In this binary, the area helper at RVA `0x2B1C40` reads the same `+0x04` field
as the getter at `0x2B1C80`, shifts the unsigned value right by one, and uses
single-precision arithmetic with a pi constant before truncating the result.
Thus its area calculation uses **`floor(storedExtent / 2)`**. For stored
extent `20`, that intermediate value is `10`.

This corrects an overly literal reading of the historical name `Radius`.
It does **not** establish a universal metre/cell conversion for every
placement-distance getter or the exact edge-cell inclusion rule. Keep the
stored/API value in diagnostics and measure the boundary in the game when
calibrating a change. Exact instructions and the constant at `0x73583C` are
included in the evidence.

## Existing Plus controls

The complete command and native-hook reference remains in
[Outposts](outposts.md). These controls already exist in Plus:

| Getter / setter pair (`Lifx::` prefix) | Baseline | What the setter changes |
| --- | --- | --- |
| `getOutpostDefaultRadius` / `setOutpostDefaultRadius` | `20` | Default used for newly created outpost land; does not resize existing rows |
| `getMonumentMinDistance` / `setMonumentMinDistance` | `150` | Guild monument placement separation |
| `getOutpostOutpostMinDistance` / `setOutpostOutpostMinDistance` | `300` | Outpost separation from outposts / guild land |
| `getOutpostMinDistanceToPersonalClaim` / `setOutpostMinDistanceToPersonalClaim` | `20` | Outpost-to-personal placement check |
| `getMonumentMinDistanceToPersonalClaim` / `setMonumentMinDistanceToPersonalClaim` | `20` | Monument-to-personal placement check |

These setters update process-local atomics. They do not write a persistent
startup configuration. To keep an override after restarting the process,
reapply it through the deployment's existing startup mechanism **after the
commands have been registered**. Inspect the getters and startup warnings:
the two personal-claim call-site patches can fail independently, so a getter
returning the requested value alone does not prove the placement check uses it.

Current setters use `strtoul` with base `0` and no end-pointer validation;
their zero check is not complete input validation. Supply deliberate positive
decimal integers within the intended gameplay range. Do not use negative,
overflowing or partly numeric strings as a way to disable a rule.

### Resizing an existing outpost

`Lifx::setOutpostRadius(guildLandID, newRadius)` calls the engine's
`changeGuildLandRadius` (`CmOffset::OUTPOST_CHANGE_GUILD_LAND_RADIUS`,
RVA `0x2D0F00`) with `(uint64_t(guildLandID) << 32) | 0x01`.

The wrapper does not look up `guild_lands.LandType=4` before calling the
engine. The `0x01` tag is not protection against supplying an ID from a
different table: it is synthesized from whatever number the caller supplies.
First resolve and verify the intended **outpost land row**, including its
owner and location. Do not pass `claims.ID` or the ID from `dumpOutposts()`.

The existing native path handles persistence and boundary rebuilding, but
the wrapper ignores its return value and prints its echo after the call.
Treat that echo as an invocation report. Confirm the saved `Radius` and the
in-game boundary before reporting a successful resize. Changing the global
default is a separate operation.

## Personal claim resize lifecycle

The reference SQL uses `personal_lands.IsTemp` as a state, not a boolean:

| Value | Meaning in the procedures |
| --- | --- |
| `0` | Active personal land |
| `1` | Pending rectangle for a resize |
| `2` | Deleted land retained as a reusable row |

`p_queueResizePersonalLand` creates or updates an `IsTemp=1` row and returns
its `TemporaryPersonalLandID` together with the **existing** active `ClaimID`.
It does not immediately replace the active corners.
`p_onPersonalLandMaintenance` copies the pending corners and supplied support
points into the active row, then removes the temporary row. A pending and an
active row for the same character therefore do not by themselves indicate
duplicate ownership.

`p_deleteLandAndClaimByClaimId` removes the claim and associated rules, while
marking the personal land `IsTemp=2`. Do not treat every retained personal row
as an active claim. The stored procedures explain persistence transitions;
they do not establish the maintenance schedule for a particular running world.

## Read-only diagnostics

Replace `123` with the intended guild ID, character ID, or claim ID for each
query; the same number below is only an example. Run queries against the
correct world database using your normal [database access](mysql_access.md).

```sql
-- Guild and outpost land identities, not building IDs.
SELECT gl.ID AS GuildLandID, gl.GuildID, gl.LandType,
       gl.CenterGeoID, gl.Radius, gl.SupportPoints, c.ID AS ClaimID
FROM guild_lands AS gl
LEFT JOIN claims AS c ON c.GuildLandID = gl.ID
WHERE gl.GuildID = 123 AND gl.LandType IN (3, 4);

-- Include pending/deleted states when diagnosing a personal resize.
SELECT pl.ID AS PersonalLandID, pl.IsTemp, pl.GeoID1, pl.GeoID2,
       pl.SupportPoints, c.ID AS ClaimID
FROM personal_lands AS pl
LEFT JOIN claims AS c ON c.PersonalLandID = pl.ID
WHERE pl.CharID = 123;

-- Inspect saved rules and subject selectors for one ClaimID.
SELECT s.CharID, s.GuildRoleID, s.GuildID, s.StandingTypeID,
       r.CanEnter, r.CanBuild, r.CanClaim, r.CanUse, r.CanDestroy
FROM claim_rules AS r
JOIN claim_subjects AS s ON s.ID = r.ClaimSubjectID
WHERE r.ClaimID = 123;
```

The examples are SELECT-only and contain no player records. Directly editing
land/claim rows while the server has loaded them is not equivalent to invoking
the engine's update path. For a change, compare saved data, loaded behavior
and client rendering; use an ordinary character when checking access so GM
privileges do not mask the result.

## Status & provenance

LiFXXL is a running community server project. This contribution organizes
ordinary-claim findings from that work and checks them against reference
SQL/scripts, the stock `1.4.4.5` server image, and the current Plus source.
The [evidence file](evidence/claims_and_settings_reference.json) includes the
binary fingerprint, eight exact native instruction ranges, five constants,
eleven selected source excerpts and the API limitations described above.

The reference defaults and SQL transitions were read directly; current
LiFXXL database values, startup overrides and in-game boundaries were not
queried for this contribution. No new server session, deployment, restart or
database change was performed. Runtime calibration of a changed size and
permission-resolution precedence remain separate from this source review.
