---
title: Larger maps — full-world physics bounds
author: Zbigi
status: re
domain: reverse-engineering
tags: [physics, collision, broadphase, terrain, maps]
related: [offsets.md, server_clean_install.md, ai_and_spawning.md]
sources: [docs/evidence/physics_world_bounds_reference.json, docs/offsets.md]
updated: 2026-11-12
---

# Larger maps — full-world physics bounds

LiFXXL findings and documentation contributed by **Zbigi**.

When one server loads an entire 9 × 9 world, its collision broadphase must
cover that world's actual coordinates. In the reference server, Bullet starts
with XY limits `[-6144,6144]`; the inspected 81-terrain layout extends to
`[-9196,9200]` on each axis. The LiFXXL uniform-bounds implementation uses
`[-12288,12288]` as a conservative envelope. This adjusts collision candidate
selection; terrain loading, navigation generation and client collision remain
separate parts of making the complete world playable.

This entry describes **the complete map in one server process**. See the
[full terrain-loading contribution](https://github.com/LiF-x/Plus/pull/23)
for the matching terrain/configuration requirements. The
[reference evidence](evidence/physics_world_bounds_reference.json) contains
native bytes and selected source excerpts; it does not install this feature
in Plus.

## What the native physics limit does

The primary broadphase is `bt32BitAxisSweep3`. It converts AABB endpoints to
bounded integer coordinates and sorts them to select possible collision pairs.
The native construction uses `maxHandles=1500000`; the map extent and handle
capacity are different parameters.

Beyond the configured XY limits, endpoint quantization clamps to its boundary.
Separated objects outside the same boundary can consequently produce extra
collision candidates. This is an index effect: it does not physically teleport
the objects, prove that they contact one another, or explain every server stall.
The later collision stages still determine contact from geometry.

An auxiliary dynamic tree used for queries does not make the primary fixed-range
broadphase unbounded. The upstream [Bullet implementation](https://github.com/bulletphysics/bullet3/blob/master/src/BulletCollision/BroadphaseCollision/btAxisSweep3Internal.h)
helps explain quantized endpoints and the optional raycast accelerator; the YO
addresses and selected implementation are checked separately in the executable.
The upstream source is not assumed to match the embedded Bullet release exactly.

## Reference executable and constructor inputs

The following are RVAs for Windows x64 `ddctd_cm_yo_server.exe` **1.4.4.5**,
size `12,441,032`, preferred image base `0x140000000`. Its SHA-256 is recorded
in the evidence. Resolve RVAs against the loaded module base, not an assumed
fixed process address.

| Entry / data | RVA | Observed role |
|---|---|---|
| `BtWorld` initialization | `0x14E0D0` | Constructs the broadphase. |
| Maximum input vector | `0x789020` | Float4 `(6144,6144,2000,0)`. |
| Minimum input vector | `0x789030` | Float4 `(-6144,-6144,-1000,0)`. |
| Minimum / maximum loads | `0x14E18D` / `0x14E199` | Copy inputs onto the initialization stack. |
| Capacity argument | `0x14E1CB` | Loads `0x16E360`, decimal `1500000`. |
| Constructor call | `0x14E1DE` → `0x671A00` | Constructs `bt32BitAxisSweep3`. |
| Internal constructor | `0x671700` | Copies world bounds and derives the quantization scale. |
| `addHandle` | `0x671D30` | Quantizes/clamps endpoints when inserting a handle. |
| `createProxy` / `setAabb` | `0x672560` / `0x6729C0` | Insert and update collision proxies. |

The internal instance owns its copied minimum at `+0x10`, maximum at `+0x20`
and quantization scale at `+0x30`. Changing the constructor input vectors after
world construction does not update an existing instance's scale or sorted
endpoints. Apply the intended inputs during startup before construction.

## Exact terrain coordinates and the 9 × 9 example

**Correction by Zbigi:** older notes used a `2048`-unit stride and called
`±9216` the exact world boundary. The reference coordinate function at
`0x578EF0` multiplies terrain-column/row differences by **2044**, then subtracts
**1020**. Its float constants are at `0x8D30B8` and `0x8D30B4` respectively.
For the centered `406..486` package, with central terrain `446`:

```text
column = (TerrainID - 406) % 9
row    = (TerrainID - 406) // 9
minX   = (column - 4) * 2044 - 1020
minY   = (row    - 4) * 2044 - 1020
maxX   = minX + 2044
maxY   = minY + 2044
```

The union is `[-9196,9200] × [-9196,9200]`, with center `(2,2)` and width
`18396`. The `2044` stride corresponds to `511` GEO cells of `4` engine units.
Use these engine coordinates for collision bounds; a map generator's descriptive
metre scale is not interchangeable with them. Other layouts need their own
effective TerrainBlock union rather than reusing these numbers blindly.

| Envelope | Minimum XY | Maximum XY | Meaning |
|---|---:|---:|---|
| Stock broadphase | `-6144` | `6144` | Does not enclose this full 9 × 9 layout. |
| Actual terrain union | `-9196` | `9200` | Derived from the reference native coordinate formula. |
| Union plus chosen `3072` margin | `-12268` | `12272` | Exact arithmetic for that margin, asymmetric about zero. |
| Existing symmetric example | `-12288` | `12288` | Covers the union with `3092` below the minimum and `3088` above the maximum. |

The symmetric `12288` setting remains a valid covering example despite its older
nominal-grid derivation. It is not the exact terrain half-width or a measured
optimum. The `3072` margin is a reference engineering policy, not a demonstrated
minimum required by Bullet. Include reachable moving-object AABBs and any intended
out-of-map area when choosing a production envelope; check Z independently.

The reference keeps physics Z at `[-1000,2000]` and W at zero. Enlarging XY
does not justify changing vertical limits, collision masks, gravity, timestep,
solver iterations or player capacity.

## Applying the reference setting at startup

The LiFXXL implementation accepts this **custom extension configuration**:

```xml
<physicsWorldBounds enabled="true" halfExtentXY="12288" />
```

This is the reference extension's `cm_server.xml` syntax. Adding that element
to stock Plus `lifxpluss.xml` does not create the parser or native integration.
For a Plus implementation, port the uniform-bounds configuration and startup
lifecycle through the project's injected extension mechanism.

The inspected uniform path performs these checks:

1. Missing or disabled configuration leaves the original inputs unchanged.
   Enabled configuration requires a finite `halfExtentXY > 6144` and `<= 65536`.
   That upper bound is a parser policy, not a certified map-size limit.
2. Before installation, compare the expected **24-byte** vector-load sequence
   at `0x14E18D` and all eight original floats at `0x789020`.
3. Change the four XY values in the contiguous **32-byte in-memory input pair**,
   retaining Z/W and restoring the original page protection. The EXE on disk
   remains unchanged.
4. Reject reconfiguration/double installation. During teardown, restore only
   if the values still match this extension's installed values; report a
   conflicting writer instead of overwriting it.

Both vectors are needed. An extent-only change applied to one input, an
incorrect min/max order or a late write to constructor data does not resize
the actual physics world correctly. There is no supported live-resize command
in this reference. Changing back also takes effect when a new world is created
through the normal startup lifecycle.

## What to verify on the complete world

First verify all 81 expected TerrainBlocks and their collision geometry are
loaded. A larger broadphase box cannot supply a missing terrain, building,
object collision shape or tunnel. A navmesh polygon cannot supply it either.
The server-side change also does not establish matching client collision
coverage; evaluate the client separately using the same world layout.

For a planned normal-server validation, record the exact EXE/extension identity,
effective configuration and **copied bounds of the constructed broadphase**.
An XML entry or installer state flag alone proves less than reading the actual
instance bounds and scale.

Exercise normal-player ground/building collision on outer terrain blocks and
terrain seams, then mounted movement, carts and combat where those are used.
Check both expected contacts and expected separation. Include the intended
horizontal and vertical extremes, and repeat after a normal restart.

Measure collision candidates, tick timings and memory under comparable load.
The native capacity argument stays at `1500000`; increasing extent is not an
increase in that object pool or in client slots. A broader coordinate interval
changes quantization resolution and candidate distribution, so neither a fixed
speedup nor a linear memory increase follows from the map-size ratio.

## Status & provenance

**Author: Zbigi.** The 2026-11-12 documentation review checked the original
constructor inputs, instruction ranges, native terrain-coordinate constants
and selected uniform-bounds implementation. All published excerpts exclude
unrelated integration code. Existing LiFXXL server operation is the project
context; this review performed no new server launch, runtime deployment or
gameplay benchmark and reports no measured ping improvement.

The page records a concrete bounds mismatch and an existing startup correction.
Acceptance of a particular installed build still requires its actual world
bounds and normal-player collision observations. Navigation bounds/cache need
their own review; changing the Bullet envelope does not rebuild navigation.
