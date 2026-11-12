---
title: Larger maps — full-world navigation and cache
author: Zbigi
assistance: ChatGPT (Codex)
status: re
domain: reverse-engineering
tags: [navmesh, navigation, terrain, maps, cache, ai]
related: [ai_and_spawning.md, animal_spawn.md, server_clean_install.md]
sources: [docs/evidence/navmesh_world_bounds_reference.json, docs/evidence/navmesh_cache_inspect.py, docs/animal_spawn.md]
updated: 2026-11-12
---

# Larger maps — full-world navigation and cache

These documentation updates were generated with help from ChatGPT (Codex),
in collaboration with Zbigi, from notes and analyses collected over four months
of work on LiFXXL, a community project based on a modified LiF:YO server.

For a complete 9 × 9 map in one server, configure `GlobalNavMesh` before world
loading, load all intended terrain geometry, and use a cache generated for the
same box and world state. A terrain-count change does not enlarge an existing
navigation cache. The LiFXXL full-world reference uses position `(0,0,1500)`
and scale `(9210,9210,500)`, producing game-space bounds
`[-9210,9210] × [-9210,9210] × [1000,2000]`.

The [evidence](evidence/navmesh_world_bounds_reference.json) includes selected
source, native instruction checks and a complete record/base-vertex inspection
of the current 9 × 9 package. Its cache contains **952,081 tile records** and
base vertices in **all 81 terrain footprints**. This is a concrete cache with
outer-world navigation, while graph connectivity and actual path following
need separate checks.

See [AI behaviors and spawning](ai_and_spawning.md) for the behavior layer,
[animal spawning](animal_spawn.md) for native animal integration, the
[full-terrain contribution](https://github.com/LiF-x/Plus/pull/23) for loading
the whole world and the [physics-bounds contribution](https://github.com/LiF-x/Plus/pull/25)
for collision bounds. These are separate requirements; widening Bullet's
collision index does not generate navigation polygons.

## Three different kinds of coverage

| Layer | What must cover the intended area |
|---|---|
| Terrain and collision | Loaded terrain/object/tunnel geometry and a physics world that encloses it. |
| Navmesh generation | The actual `GlobalNavMesh.getWorldBox()`, build parameters and source geometry. |
| Navigation cache and queries | Matching cache origin/capacity, current world changes, usable polygons and connected paths. |

Changing `actorRadius`, `actorClimb` or `walkableSlope` changes which surfaces
are considered suitable for an agent; these fields do not enlarge the world
box. A larger mesh also does not automatically give a locally controlled
`Player` a route-planning policy. Its controller must request and follow routes;
native movement simulation alone is not that policy.

## Configure the box before loading or generating navigation

The inspected stock script creates:

```torquescript
new NavMesh(GlobalNavMesh)
{
    .position = "0 0 1500";
    .scale = "3066 3066 500";
    .actorRadius = 2;
    .actorClimb = 1;
    .walkableSlope = 50;
};
```

In the inspected LiFx startup, `initServer()` executes `scripts/navmesh.cs`,
calls `initNavMeshUpdates()`, and then executes `onInitServerCallbacks` before
returning. The reference `LifXXLNavMesh::setup()` registers `configureBounds`
there. This gives it the created `GlobalNavMesh` before `createWorld()` consumes
navigation. `onPostInit` is too late for configuring the first load/build.
Verify this ordering in the framework version being ported.

The core of a **single-server 9 × 9 initialization callback** is:

```torquescript
// Inside the early initialization callback, after checking the 9x9 layout
// and that GlobalNavMesh exists.
GlobalNavMesh.setTransform("0 0 1500 0 0 1 0");
GlobalNavMesh.setScale("9210 9210 500");
%box = GlobalNavMesh.getWorldBox();
if (getWord(%box, 0) != -9210 || getWord(%box, 1) != -9210 ||
    getWord(%box, 2) != 1000 || getWord(%box, 3) != 9210 ||
    getWord(%box, 4) != 9210 || getWord(%box, 5) != 2000)
{
    error("GlobalNavMesh does not match the selected full-world box.");
    quit();
    return;
}
```

This is an integration excerpt, not a command to resize navigation after players
have entered the world. The reference refuses unsupported/non-square layouts,
missing mesh objects and an unexpected resulting box. A successful box check
means the generation envelope is configured; it does not mean the cache has
loaded, building has finished or a path exists.

## The geometry correction and compatible full-world settings

**Correction:** `N*1024-6` is the historical full-world navmesh sizing
rule retained by the reference mod. It must not be described as exact terrain
geometry. The native terrain-coordinate function `0x578EF0` uses a stride of
**2044** and a corner bias of **1020**. For IDs `406..486` around terrain `446`,
the actual 9 × 9 terrain union is `[-9196,9200]` on both horizontal axes,
centered at `(2,2)`.

The existing symmetric `±9210` navmesh envelope covers that union and matches
the inspected full-world cache. It extends `14` units below the terrain minimum
and `10` above the maximum. Multiplying the stock scale by three gives `9198`;
with the original zero center that would stop short of the positive terrain
edge. An exact `9198` half-width requires center `(2,2)` and a different cache
origin. Do not change only one of those values or reuse a mismatched cache.

| Reference full-world grid | Half-scale X/Y | Navmesh game Z | Origin in Detour axes | Capacity at tile size `16×16` |
|---|---:|---|---|---:|
| 3 × 3 | `3066` | `1000..2000` | `(-3066,1000,-3066)` | `384² = 147456` |
| 5 × 5 | `5114` | `1000..2000` | `(-5114,1000,-5114)` | `640² = 409600` |
| 9 × 9 | `9210` | `1000..2000` | `(-9210,1000,-9210)` | `1152² = 1327104` |

The 5 × 5 row is supported sizing arithmetic, not verification of a complete
5 × 5 map package. These scales describe the reference full-world policy;
an offset, rectangular or differently sized world requires its actual box.

Recast/Detour coordinates are `(game X, game Z, -game Y)`. For an arbitrary
game-space box, the Detour origin is therefore `(minX,minZ,-maxY)`. Use the
negative **maximum** Y, not the negative minimum. Physics remains in the game's
`(X,Y,Z)` axes and has different vertical limits.

Keep the reference agent settings while validating the horizontal expansion.
Increasing radius/climb/slope simultaneously changes the generated graph and
obscures the reason for changed behavior. Retain the `[1000,2000]` vertical
envelope only when it suits the intended walkable geometry. Underwater terrain
or steep surfaces can legitimately have no land-navigation polygons; absence
of a polygon at one point is not proof of a world-size failure.

## Native build/load contract

The following server RVAs are for the `1.4.4.5` executable identified in the
evidence: size `12,441,032`, preferred base `0x140000000`.

| Entry / field | RVA / offset | Relevant behavior |
|---|---|---|
| World navigation initialization | `0x5880D0` | Reaches both cache loading and building. |
| Load navmesh | `0x4D4E40` | Reads the outer header and passes stored parameters to Detour. |
| Parameter initialization | `0x4D4EF9..0x4D4F01` | Uses parameters beginning at file byte `12`. |
| Build navmesh | `0x4D33D0` | Builds from the configured object box. |
| Prepare build configuration | `0x4D5860` | Converts the object's game-space box to build coordinates. |
| World box | Mesh `+0x31C` | Box consumed by the build path. |
| Tile capacity arithmetic | `0x4D3485..0x4D34FE` | Computes horizontal tile dimensions from box span/tile size, then multiplies them. |
| Detour initialization / tile insertion | `0x6C6F00` / `0x6C5020` | Initialize parameters and add tile data. |

For the observed layout:

```text
tilesX   = ceil((maxX - minX) / tileWidth)
tilesY   = ceil((maxY - minY) / tileHeight)
maxTiles = tilesX * tilesY
9x9 example: ceil(18420 / 16)² = 1152² = 1327104
```

`maxTiles` is capacity, not a requirement to have a polygon in every cell.
`maxPolys=128` in the inspected file is a separate per-tile capacity parameter.
Tile count, terrain count and connected-agent count are different quantities.
Do not alter those integers in an existing file header to imitate a larger
world: no new geometry or valid references are created that way.

## Cache compatibility and the current 9 × 9 artifact

The observed outer format is little-endian, **40 bytes**:

| File offset | Field |
|---|---|
| `0` / `4` / `8` | U32 magic `0x4D534554`, version `4`, serialized tile count. |
| `12` / `16` / `20` | Float origin in Detour X/Y/Z axes. |
| `24` / `28` | Float tile width / height. |
| `32` / `36` | U32 maximum tiles / maximum polygons per tile. |

Each tile record has a **16-byte** wrapper: U64 tile reference, U32 payload
length, and four bytes whose meaning is not required by this inspector.
The payload starts with `0x444E4156`, version `7`, and a **100-byte** mesh
header. Base vertices follow as float XYZ triples. The layout was compared
with the primary [Detour header](https://github.com/recastnavigation/recastnavigation/blob/main/Detour/Include/DetourNavMesh.h);
YO's outer wrapper and loader were inspected separately. Modern upstream
settings or reference widths must not be assumed compatible with the game.

The current full-world package artifact
`dev/mapSize/9/server/navMeshes/nm1.nmesh` was inspected read-only:

| Measurement | Result |
|---|---:|
| File size | `1,478,356,300` bytes |
| Origin / tile size | `(-9210,1000,-9210)` / `16×16` |
| Capacity | `1327104` tiles, `128` polygons per tile |
| Serialized records consumed to exact EOF | `952081` |
| Declared polygons / inspected base vertices | `7487435` / `13898773` |
| Game X range of base vertices | Approximately `-8582.800..8462.800` |
| Game Y range of base vertices | Approximately `-8458.600..8560.000` |
| Game Z range of base vertices | Approximately `1000.200..1346.600` |
| Terrain footprints containing base vertices | `81/81` |

**Historical correction:** the September audit found an older package cache
with central 3 × 3 coverage. The currently inspected file has a different size,
origin and tile population and extends well outside that central area. Do not
carry the older finding forward as the current state of this package. A package
file also does not identify which cache a running process selected.

The scan checks framing, tile format, nonzero unique references, finite base
vertices and exact EOF. It does not validate detail triangles, link consistency,
path connectivity or native query behavior. Having vertices in all footprints
does not mean every traversable point or terrain seam is connected. Tile-header
bounds may include generation padding; they need not equal the configured box
or the actual vertex bounds.

Repeat the inspection with the included read-only tool:

```text
python docs/evidence/navmesh_cache_inspect.py path/to/nm1.nmesh --grid 9
```

The tool uses only Python's standard library and writes its JSON report to
stdout. `--grid` controls the centered native terrain-footprint statistics; it
does not modify the file or change its generation parameters. The public
evidence contains aggregate measurements, not individual map-file hashes.

## Rebuilding and keeping the result

Configure the intended box **before** loading/building. A larger object box
cannot enlarge polygons already serialized with a smaller origin/capacity.
Equally, rebuilding with the old object box repeats the old spatial limit.

During an intended world update, keep a consistent recoverable world/cache
state, validate the selected package and every candidate cache, and prevent
the map publisher from restoring an obsolete base file. The reference validator
checks format, finite parameters, origin, positive tile dimensions and expected
capacity. Its header check is a compatibility check, not the full scan above.

Let the game's load/build/save path reconcile the selected base cache, any
versioned cache and the world's navigation metadata. File presence alone does
not establish that the cache selected by the engine exists, matches the world
or includes pending terrain/forest/object changes. Blindly deleting only a
cache file can leave metadata pointing at a missing file and does not ensure
a correct rebuild.

After construction, wait for navigation completion and server readiness, inspect
the generated cache, and record which file/version the engine actually loads
on the next normal start. Preserve that generated result through subsequent
map publication. Review geometry changes, including tunnels, through the
engine's navigation-update path; increasing the initial box is only one part
of maintaining the graph.

## Startup cost and gameplay acceptance

At tile size 16, the 9 × 9 reference capacity is nine times the 3 × 3 capacity.
That is not a prediction of exactly nine times the elapsed time or memory.
Walkable surface, obstacles, terrain/forest changes and tile complexity matter.
The vanilla startup timing in [the clean-install guide](server_clean_install.md)
must not be treated as a 9 × 9 startup promise.

A historical **real-server** report from 2026-09-11 recorded a base-cache read
of `7.874 s`, followed by `82 min 26.793 s` from the first forest-update marker
to the first tile-progress marker, then `21 min 34.032 s` until navigation
completion. The first progress message still reported `484000 tiles left`.
Those are phase observations for that run, not a benchmark of today's inspected
artifact or proof that all of either interval was CPU time in one function.
A cached world can still spend substantial time applying changes and rebuilding.

For the intended full-world server, check all of the following during normal
gameplay acceptance:

1. All 81 terrains are actually loaded, and the mesh reports the intended box.
2. The engine selects a compatible cache or completes its native generation;
   changes and cache metadata persist coherently.
3. Animals/NPCs can obtain and follow paths on suitable land in outer rows,
   columns and corners, and across terrain seams. Confirm the native spawn
   lifecycle, rather than assuming a rendered object is navigation-integrated.
4. Check representative obstacles and changed terrain/tunnels, including routes
   which should remain blocked. Separate missing polygons from disconnected
   paths, unsuitable slope, wrong agent settings and behavior-controller faults.
5. Repeat the relevant routes after a planned restart and measure startup time,
   steady tick cost and memory with the intended population.

## Status & provenance

The 2026-11-12 review compared native build/load code,
initialization ordering, full-world configuration and the entire current
cache's record framing/base vertices. It also retains explicitly dated
real-server startup evidence. This documentation review did not launch or
restart a server, regenerate a game cache or execute new in-game route tests.

The full-world source and cache demonstrate an existing larger navigation
envelope. Verification of the currently deployed process, complete graph
connectivity and gameplay behavior remains specific to the selected world and
build. This contribution supplies documentation and an inspection tool; it
does not deploy `LifXXLNavMesh` or add its commands to Plus.
