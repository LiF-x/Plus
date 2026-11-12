---
title: Larger maps — full-world terrain loading on one server
author: Zbigi
assistance: ChatGPT (Codex)
status: re
domain: reverse-engineering
tags: [terrain, maps, loading, configuration, startup]
related: [client_grid_patch.md, server_clean_install.md, loader_and_injection.md]
sources: [docs/evidence/full_world_terrain_reference.json, docs/client_grid_patch.md, docs/server_clean_install.md]
updated: 2026-11-11
---

# Larger maps — full-world terrain loading on one server

These documentation updates were generated with help from ChatGPT (Codex),
in collaboration with Zbigi, from notes and analyses collected over four months
of work on LiFXXL, a community project based on a modified LiF:YO server.

A larger world requires matching grid configuration, terrain identities, complete
server geology, matching client data and initialization of the entire selected
world. Increasing a client size constant alone does not generate terrain or load
it on the server. This guide describes **one server process loading the complete
selected map before normal play**.

LiFXXL is a community server project with players. For this contribution, the
[evidence](evidence/full_world_terrain_reference.json) checks the reference
executables, configuration sources and all 81 terrain triplets in a 9 × 9
baseline. Individual map-file fingerprints remain local; the public record
contains the aggregate validation results.

## Grid size and data identity

The reference server script initializes:

```text
$cm_config::Server::TerrainXCount = 3
$cm_config::Server::TerrainYCount = 3
$cm_config::Server::ServerXCount = 1
$cm_config::Server::ServerYCount = 1
$cm_config::Server::TerrainSkipCount = 441
$cm_config::Server::ServerSkipCount = 0
```

For this single-server layout, terrain dimensions determine the number of
terrain blocks. Keep the server-count dimensions at one. They are not the
terrain dimensions.

| Square grid | Terrain blocks | Relative area versus 3 × 3 | Geological columns at 511 × 511 per block |
| --- | ---: | ---: | ---: |
| 3 × 3 | 9 | 1 | 2,350,089 |
| 5 × 5 | 25 | 25/9 | 6,528,025 |
| 9 × 9 | 81 | 9 | 21,150,801 |

This is geometry/work-count arithmetic, not a linear RAM or player-capacity
prediction. A 5 × 5 row describes the required 25-block layout; it does not
assert that a complete 5 × 5 data package is included in Plus.

Terrain identity must match the package. The reference vanilla 3 × 3 uses
`TerrainSkipCount=441`, with IDs `442..450`. The inspected LiFXXL 9 × 9 package
uses `TerrainSkipCount=405`, with IDs `406..486`. Its 81 index headers independently
confirm those IDs, and the current configuration-override source sets 405 for
that package.

**Correction:** an older LiFXXL configuration note kept 441 for every
size. That is not correct for the inspected `406..486` package. Do not change
IDs based only on filenames, the world/database ID or an old template. Keep
configuration, embedded identities and database terrain records consistent.

## Configure early, then verify the loaded values

For the inspected 9 × 9 package, the intended values are:

```torquescript
// Example for a complete package whose terrain IDs are 406..486.
$cm_config::Server::TerrainXCount = 9;
$cm_config::Server::TerrainYCount = 9;
$cm_config::Server::ServerXCount = 1;
$cm_config::Server::ServerYCount = 1;
$cm_config::Server::TerrainSkipCount = 405;
$cm_config::Server::ServerSkipCount = 0;
```

Apply the selected values after stock configuration assignments and before
terrain identities, database terrain state and world initialization consume
them. Applying them after the world is initialized does not resize existing
terrain objects. In the inspected LiFXXL source, an initialization observer
applies the override between the `Loading CmConfiguration` and
`Init of DB interface` markers, then reads the actual variables back.

This is a description of that source's integration point, not a new command
or configuration option supplied by Plus. A Plus port must use its own startup
integration and injected extension path. Client configuration must agree with
the same layout before world entry. A later stock assignment can overwrite an
otherwise correct value, so check the effective values rather than just the
template text.

## Provide the whole terrain data set

For each selected terrain ID, the server baseline requires its matching set:

| File | Role |
| --- | --- |
| `t<ID>.ter` | Terrain surface asset. |
| `t<ID>.ter2idx` | Index into server geological column records. |
| `t<ID>.ter2dat` | Server geological levels. |

The inspected format has a 48-byte index header followed by 511 × 511 entries
of eight bytes each: `(U32 recordOffset, U32 levelCount)`. Server data records
are eight bytes; offsets are **record indices**, so their byte location is
`recordOffset * 8`. For each column, require
`(recordOffset + levelCount) * 8 <= data_file_size`.

The inspected baseline indexes are `2,089,016` bytes each. Header fields include
GeoVersion at byte 8, data-record size at byte 20 and TerrainID at byte 44.
Check those fields against the intended world. The 81-triplet inspection covered
243 files and every one of the 21,150,801 column-index ranges.

Client `.ter2idxcl/.ter2datcl` files are a different format. The inspected
projection tool emits four-byte surface records from the server baseline.
Do not rename those files into the server geology set: they do not replace
the full underground level data needed for mining and tunnels.

Keep the corresponding forest/object content, navigation data, terrain database
records and applicable geological modifications consistent with the package.
Baseline GeoVersion and retained `geo_patch` operations must describe the same
history. File-count validation alone does not prove that player modifications
or database versions match. A prepared baseline is not a replacement for a
consistent running world's persisted state.

## Client size constant: exact finding and limits

The [historical client-grid page](client_grid_patch.md) records file offset
`0x189554`. On the inspected reference `yo_cm_client.exe`:

| Property | Value |
| --- | --- |
| File size | `24,607,688` bytes |
| SHA-256 | `1D3F7FD3E64B119DC032CDA77D353AFEFC340AF46C6B7AE30890A9BC99E37999` |
| Preferred image base | `0x140000000` |
| File offset | `0x189554` |
| Corresponding client RVA | `0x18A154` |
| Containing instruction | `0x18A153: mov ebx, 3` |
| Containing function | `0x189F10..0x18A30A` (exclusive end) |

The byte is the first byte of a **32-bit immediate**. The surrounding code can
select 1 instead of 3, stores the selected value into two axis-size fields
(`+0x198/+0x19C`) and passes the dimensions to `0x18A600`.

This directly identifies a client size-selection site. It does not by itself
establish that one byte is a universal world-coordinate clamp, or that changing
it fully enables a larger world. The reference instruction and PE mapping
supersede the older page's lack of an identified client image/section mapping.
An extension must handle the relevant size-selection behavior and matching
configuration through the permitted injected client path, rather than editing
the executable on disk.

## Full server initialization and its cost

For server `ddctd_cm_yo_server.exe` 1.4.4.5, size `12,441,032`, SHA-256
`ACD99BD1A785D95B06494CBADE51A2C14EE60A521D0E88A4BE6AE1B3B685219F`,
`TerrainDeformer::InitTerrainList` occupies `0x588B40..0x5891C9` according to
the PE runtime-function table. Its later initialization loop reads the registered
terrain count at `0x588FA4`, obtains an array element at `0x58903C` and calls
`RebuildAllTunnels` at `0x58905A` for each non-null element.

That loop operates on registered terrains. It cannot discover missing files
merely because a configuration value says 9 × 9. Establish the complete expected
ID set, create/attach the matching TerrainBlocks and check the completed set
before opening normal play. A repeated log line or 81 files on disk does not
by itself prove 81 distinct terrains finished initialization.

Full loading includes geology and tunnel reconstruction, forest/object setup
and navigation readiness where applicable. The stock tunnel phase visits every
column even in a map with no tunnel blocks. The separate
[tunnel contribution](https://github.com/LiF-x/Plus/pull/22) explains that work
and how an accelerated rebuild must preserve effective geology and finalization.

Increasing from 3 × 3 to 9 × 9 gives nine times as many terrain blocks and
columns. Measure peak and steady memory, initialization phases, I/O and tick
time for the complete world. Do not estimate supported players from map size
alone. Likewise, selecting fewer terrain blocks means choosing a smaller world
and consistent data set; it is not a percentage-of-terrain memory setting for
an otherwise unchanged inhabited map.

## Verification for the intended full-world setup

1. Record the executable/mod identity, selected layout, effective variables and
   expected terrain IDs.
2. Validate every server triplet, embedded ID, index/data range and version;
   reconcile database terrain state and retained changes with the baseline.
3. Confirm all expected TerrainBlocks initialize exactly once, with geology and
   tunnel setup complete. Keep normal entry after full-world readiness.
4. With a matching client, visit outer edges and corners, cross terrain seams,
   check collision, objects and tunnels, then verify saved changes after a
   planned restart. A center spawn validates only a small part of a large world.
5. Record startup and steady-state resource use under the intended population.

## Status & provenance

The review verified both reference binary fingerprints, four instruction ranges,
the client file-offset/RVA conversion, configuration source and all 81 baseline
triplets on 2026-11-11. Native addresses and aggregate checks are public; the
per-file map fingerprint inventory remains local. These checks support the
documented layout and mechanisms. They do not claim a newly executed Plus
81-terrain gameplay session or install a map-loading implementation.
