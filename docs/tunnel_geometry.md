---
title: Tunnel geometry, material quantities and startup rebuild
author: Zbigi
status: re
domain: reverse-engineering
tags: [tunnels, geometry, quantities, geology, startup]
related: [offsets.md, farming.md]
sources: [docs/evidence/tunnel_geometry_reference.json, source/server/hooks/engine/hook_tunnel_drop.cpp, docs/offsets.md]
updated: 2026-11-11
---

# Tunnel geometry, material quantities and startup rebuild

LiFXXL production investigations and documentation contributed by **Zbigi**.

Tunnel shape, work/material quantities and startup reconstruction are connected.
Increasing a compiled height from 20 to 22 altitude steps can overflow a U16
material quantity; skipping a slow rebuild can leave persistent geology without
its runtime tunnel geometry. Preserve these invariants when extending tunnels.
The [existing material-reward reference](offsets.md#tunnelling-material-rewards)
and Pabluuz's extra-drop hook concern inventory rewards, a separate operation.

## Reference build and evidence

All native addresses are server RVAs for Windows x64 `ddctd_cm_yo_server.exe`
1.4.4.5, size `12,441,032`, SHA-256
`ACD99BD1A785D95B06494CBADE51A2C14EE60A521D0E88A4BE6AE1B3B685219F`.
Use the loaded module base under ASLR; preferred base `0x140000000`.
The [evidence](evidence/tunnel_geometry_reference.json) preserves 11 instruction
ranges, script defaults, arithmetic and dated LiFXXL runtime observations.

## Top altitude is different from height

Forward, downward and upward tunnelling call the common implementation at
`0x3ADA30` from `0x3A69C3`, `0x3A6DB3` and `0x3A71A3`. Its geometry path uses
`TerrainDeformer::DigConvex` at `0x5864F0`.

At `0x58681B`, stock initialization sets `r15d=20`; at `0x586821`, it sets
`r14d=initial_floor+20`. At the later check `0x586B66`:

```text
r14w = newAltitude       # top of the candidate air layer
r15w = newBlockGeoHeight # thickness, not an absolute lower coordinate
floor = top - height
```

The subtraction is explicit at `0x586BA3..0x586BAA`. Treating `r15w` as a floor
coordinate and testing `r14-r15 >= 22` tests floor altitude, not clearance.

The slope adjustment at `0x586B29` changes the top while preserving height,
so both floor and ceiling move together. In the reference configuration,
`$cm_config::Terrain::DigAdjust=1`; the native adjustment corresponds to five
altitude steps, or 0.5 m. For an illustrative 22-step (2.2 m) enclosed segment:

| Direction | Top | Height | Resulting floor |
| --- | --- | --- | --- |
| Forward | `F + 22` | `22` | `F` |
| Upward | `F + 27` | `22` | `F + 5` |
| Downward | `F + 17` | `22` | `F - 5` |

Do not add a second floor adjustment. Ordinary wall-side tunnelling calls
`_checkTunHeight` at `0x58D3B0` from `0x586B55`; it can adjust height at the
natural terrain surface. The caller updates `top = old_top - old_height +
new_height`, preserving the floor. An open hillside entrance is consequently
not required to have the same enclosed roof as a deeper segment.

## Why changing 20 to 22 can reduce digging work

At `0x5907FE`, the split-level builder obtains the generated level height.
At `0x590806`, it calls `Geo::QuantityPerLevel` (`0x4BF7D0`), then multiplies
the values in a 32-bit register at `0x590811`. At `0x590C3E` it passes only
`r12w`, the low 16 bits, into the level-construction arguments.

The reference script sets `QuantityPerLevel=3000`:

| Generated height | Full product | Stored U16 quantity |
| ---: | ---: | ---: |
| 20 | 60000 | 60000 |
| 22 | 66000 | 464 |
| 28 | 84000 | 18464 |

This is wrapping, not saturation. The 22-step case leaves only 464 material
units, which an action doing 2000 effective damage can consume immediately.
The historical LiFXXL investigation recorded exactly 464 for the failed enclosed
case, and `18464 - 2000 = 16464` for a surface-intersecting case. Those real-game
observations agree with the native arithmetic; they are not new measurements
of the currently loaded world.

For a positive quantity-per-level `Q`, the largest directly representable
product has `height <= floor(65535 / Q)`. At 3000 that bound is 21 steps.
The evidence supplies exact integer examples; floating-point rounding is not
the cause of this particular loss.

### Extension contract for a taller enclosed tunnel

Separate the stock work calculation from the final cavity. The LiFXXL reference
keeps the 20-step work geometry and transforms a completed normal 20-step
builder request to height 22 while raising its top by two, preserving the floor.
The relevant final Geo-change builder is `0x58F240`, with tunnel call return
`0x586CC2`. Surface clipping remains owned by `_checkTunHeight`.

The reference also caps the generated material product to the stock work amount
`min(generated_height * Q, 20 * Q)` before U16 conversion. At `Q=3000` that
ceiling is 60000. This is a specific material-accounting policy, not a universal
repair for arbitrary `Q`: even `20 * Q` must fit U16, and a different policy
must preserve material conservation. Do not globally change material density
or add inventory rewards merely to conceal an overflow.

Plus's existing `hook_tunnel_drop.cpp` awards configured extra inventory items
after the original material operation succeeds. It does not widen Geo quantities
or change the tunnel cavity. An extra-drop hook cannot repair the wrapped work
quantity by itself. Keep runtime extensions in the injected mod path; do not
ship edited game executables.

## Persistent geology and runtime tunnel blocks

Tunnel geometry is reconstructed from geological air levels and surrounding
solids. Baseline `.ter2idx/.ter2dat` data and applicable `geo_patch` changes
jointly determine the effective columns. `.ter2idx` contains offsets and level
counts; multiple levels can be ordinary soil/rock strata and do not prove a
tunnel exists.

During startup, the registered-terrain loop calls `RebuildAllTunnels`
(`0x589C10`) at `0x58905A` for each non-null terrain. The rebuild reads GeoData
dimensions at `+0xE0/+0xE4` and visits every Y/X column. Its main calls are:

| Call site | Target | Role |
| --- | --- | --- |
| `0x589D35` | `0x5A8DE0` | Process the column's tunnel state. |
| `0x589D4F` | `0x4B1300` | Read the first layer for the surface maximum. |
| `0x589DCC` | `0x597D20` | Rebuild terrain lists. |
| `0x589DD6` | `0x4AE490` | Release the GeoData cache-disable scope. |
| `0x589DDB` | `0x58E560` | Finalize tunnel state. |

At 511 × 511 columns per terrain, a 3 × 3 world has 2,350,089 columns;
9 × 9 has 21,150,801. A world with no tunnel blocks still incurs stock column
enumeration. Disabling the rebuild is not a valid replacement for reconstructing
collision and geometry.

A conservative acceleration can scan baseline air columns and union them with
**every patch-touched column**, then pass candidates through the native builder
in the stock order. Reading only `geo_patch` misses air already incorporated
into a compacted baseline. The surface maximum must also use effective patched
heights; a patch can lower the old maximum. Preserve manager initialization,
cache lifetime and finalizer ordering even when there are zero candidates.

The historical LiFXXL 81-terrain comparison recorded 204,722 ms for the
instrumented stock phase and 6,195 ms for the optimized phase. That snapshot
produced zero effective tunnel blocks: it verifies the measured startup path,
candidate discovery and surface result, but cannot alone establish geometry
and collision correctness for an occupied tunnel network.

## Practical verification

Use fresh enclosed cells as well as hillside entrances. Check forward segments,
repeated upward/downward segments and turns. Record effective digging damage,
remaining level quantity, produced materials, floor/ceiling and traversability.
Existing tunnels are persisted geometry; changing a future builder is not a
retroactive conversion.

For rebuild changes, compare a world containing real tunnels before and after
restart and compaction, including filled/removed tunnels. Check block counts,
surface maxima, geometry, collision and normal movement inside the tunnel.
Keep baseline/version/patch data consistent; deleting runtime blocks or patch
rows directly does not implement geological tunnel removal.

## Status & provenance

**Zbigi** checked the reference instructions, script values and calculations on
2026-11-11. The contribution draws on the operating LiFXXL project and retains
the scope of its historical geometry and startup observations. The public JSON
contains source fingerprints and the native evidence needed to inspect the
mechanisms without disclosing player data. This PR documents the behavior;
it does not install a geometry or rebuild hook in Plus.
