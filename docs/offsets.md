---
title: Reverse-engineering offset encyclopedia
status: re
domain: reverse-engineering
tags: [offsets, rva, tree-drops, gem-drops, tunnelling, max-players]
related: [reverse_engineering.md, farming.md, conventions.md]
sources: [source/server/hooks/engine/hook_tree_drop.cpp, source/server/hooks/engine/hook_gem_drop.cpp, source/server/hooks/engine/hook_tunnel_drop.cpp]
updated: 2026-08-27
---

# Reverse-engineering offset encyclopedia

This file collects stable RVAs and data-layout observations that are likely to
be useful in later exploration. Unless stated otherwise, addresses are RVAs
relative to the base of `ddctd_cm_yo_server.exe`; absolute addresses are omitted
because ASLR changes them between sessions.

## Verified executable build

| Property | Value |
| --- | --- |
| Product version | `yo_1.4.4.5` |
| File version | `1.4.4.5` |
| File size | `12,441,032` bytes |
| SHA-256 | `ACD99BD1A785D95B06494CBADE51A2C14EE60A521D0E88A4BE6AE1B3B685219F` |

Hooks that use these RVAs must verify surrounding machine-code bytes before
patching. Do not assume they apply to another server executable.

## Tree felling and movable-log creation

| RVA / range | Meaning |
| --- | --- |
| `+0x3712C0..+0x371B72` | Tree-felling function containing species lookup and log selection. |
| `+0x371447` | Loads the byte-sized `TreeType` from `[rbp+0x120]`. |
| `+0x37144E` | Calls the species-record lookup at `+0x483FA0`. |
| `+0x483FA0` | Maps a `TreeType` code to its species-data record. |
| `+0x3715DD` | Loads the hardwood discriminator from species record offset `+0xF4`. |
| `+0x3716D0..+0x3716E4` | Confirmed 26-byte selector for softwood `653` / hardwood `626`; inline hook site. |
| `+0x3716EA` | First instruction after the replaced selector; trampoline return RVA. |
| `+0x371830` | Reloads the selected ObjectTypeID from `[rbp+0x110]`. |
| `+0x371872` | Calls the movable-object producer at `+0x24A140`. |
| `+0x24A140` | Producer receiving a structure whose first field is the selected ObjectTypeID. |
| `+0x24A2B4` | Calls `+0x0D3790` while building the create descriptor. |
| `+0x0D3790` | Object-type helper observed returning `dword ptr [rcx+0x60]`. |
| `+0x24A52B` | Only recovered direct call to the producer at `+0x15CCB0`. |
| `+0x15CCB0..+0x15D197` | Builds `CreateMovableObj_DataHandler` and its async wrapper. |
| `+0x15CF16` | RIP-relative reference to handler vtable RVA `+0x78DFC8`. |
| `+0x15D066` | Stores consumer function RVA `+0x15F870` in the async wrapper. |
| `+0x555C60` | Async wrapper path; handler at wrapper `+0x10`, consumer pointer at `+0x20`. |
| `+0x15F870` | Large `CreateMovableObj_DataHandler` consumer. |
| `+0x15F8F1` | Calls descriptor copy `+0x158600`; useful breakpoint with ObjectTypeID already at `[rbx+0x10]`. |
| `+0x158600` | Descriptor-copy function; ObjectTypeID is copied at descriptor offset `+0x08`. |
| `+0x15FAF1` | Moves the copied ObjectTypeID toward the SQL-local field. |
| `+0x15FB46` | Loads ObjectTypeID into `r12d` for later SQL formatting. |
| `+0x15FBC5` | SQL formatting vicinity for `p_createMovableObject`; useful tracing anchor, too late to patch safely. |

### Tree-related layouts and values

```text
TreeType byte in selector frame       : [rbp+0x120]
Hardwood flag in species record       : [species_record+0xF4]
Selected ObjectTypeID in selector     : [rbp+0x110]

create descriptor +0x00               : movable instance/object ID
create descriptor +0x04               : class/flag (observed as 1)
create descriptor +0x08               : ObjectTypeID

CreateMovableObj_DataHandler +0x00    : vtable
CreateMovableObj_DataHandler +0x08    : embedded create descriptor
CreateMovableObj_DataHandler +0x10    : ObjectTypeID
```

Confirmed values:

- `TreeType 0` = apple; species flag zero; vanilla drop `653` (`0x28D`).
- `TreeType 7` = oak; species flag nonzero; vanilla drop `626` (`0x272`).
- Overriding apple with ObjectTypeID `78` (`0x4E`, chair) passed an end-to-end
  live test and produced a chair next to the felled tree.

The current ordered name table is: `0 apple`, `1 birch`, `2 elm`, `3 spruce`,
`4 pine`, `5 maple`, `6 mulberry`, `7 oak`, `8 aspen`, `9 hazel`, `10 juniper`,
`11 spinny`.

## Gem drop hooks

| RVA | Meaning |
| --- | --- |
| `+0x3A9258` | Confirmed call site for gem probability computation. |
| `+0x3A925D` | Return address used to restrict the shared probability detour to the gem path. |
| `+0x3A931F` | Confirmed item-type lookup call after vanilla gem ID selection. |
| `+0x3A9324` | Return address used to restrict the shared item lookup detour to the gem path. |

The original gem ObjectTypeID range at that selection point is `481..486`.

## Tunnelling material rewards

The observations below separate the three tunnel-digging abilities from ordinary
mining. They were recovered statically from the verified executable by following
the diagnostic strings for `DigATunnel*` and
`TerrainDeformer::AddMaterialsToInventory`.

| RVA | Meaning |
| --- | --- |
| `+0x3A69C3` | `DigATunnel` calls the common tunnel-dig implementation at `+0x3ADA30`, passing direction/change code `6`. |
| `+0x3A6DB3` | `DigATunnelDownward` calls `+0x3ADA30`, passing direction/change code `8`. |
| `+0x3A71A3` | `DigATunnelUpward` calls `+0x3ADA30`, passing direction/change code `7`. |
| `+0x3ADA30` | Common implementation reached by all three tunnel-digging abilities. |
| `+0x3ADEE9` | Tunnel-only call site for `TerrainDeformer::AddMaterialsToInventory`. |
| `+0x3ADEEE` | Return address after the tunnel-only material-reward call; suitable for restricting a shared-function detour to tunnelling. |
| `+0x3AC9A0` | The other direct call to `AddMaterialsToInventory`; it is on a different terrain-digging path and must not be treated as tunnelling. |
| `+0x584120` | `TerrainDeformer::AddMaterialsToInventory`; iterates ten-byte material entries and constructs inventory items. |
| `+0x5842CB` | Builds the normal material item for the remaining quantity. |
| `+0x58435F` | Builds the `Geo::TunnellingRockRatio` portion using material/substance code `3`. |
| `+0x5936A0` | Material-item builder used by `AddMaterialsToInventory`. |
| `+0x59379F` | Calls `+0x572DE0` to map an 8-bit material/substance code to its record. |
| `+0x5937E8` | Loads the resulting inventory ObjectTypeID from material-record offset `+0xD0`. |
| `+0x5937FD` | Validates/looks up that ObjectTypeID through the shared item-type lookup at `+0x27CA00`. |
| `+0x3A89E0` | Mining helper that constructs and adds an arbitrary ObjectTypeID to a player's inventory. Candidate for re-use by a tunnelling reward hook. |

Observed `AddMaterialsToInventory` entry layout:

```text
material entry stride                 : 0x0A bytes
material entry +0x02                  : material/substance code (U8)
material entry +0x06                  : quality-like value (U8)
material entry +0x08                  : quantity (U16)
material definition record +0xD0      : inventory ObjectTypeID (U32)
```

Uncertain, inferred signatures (must be runtime-checked before relying on them):

```text
+0x584120 AddMaterialsToInventory:
  bool (playerId, inventory/context pointer, selected material code,
        material-entry vector, float parameter, U32 parameter)

+0x3A89E0 arbitrary-item helper:
  bool (unused/context pointer, playerId, objectTypeId, quality, quantity)
```

The first parameter of `+0x3A89E0` appears unused in the recovered machine code.
Its known callers pass the mining-ability object, so a tunnelling hook should
still be live-tested before calling it with a null or synthetic context.

## Server settings and player-slot limit

The observations below concern the same verified 1.4.4.5 executable. Detailed
analysis and dump triage are in `more-slots.md`.

| RVA | Meaning |
| --- | --- |
| `+0x793830` | Null-terminated `maxPlayers` configuration key. |
| `+0xB7DC7C` | Vanilla/default `maxPlayers` value; observed as `64`. |
| `+0x45ADB0` | Generic integer configuration getter used for `maxPlayers`. |
| `+0x176E9B` | Calls the getter for `maxPlayers`. |
| `+0x176EA0` | Stores the getter result in settings field `+0x9C`. |
| `+0x1741D0` | Whole-settings copy function (`RCX=destination`, `RDX=source`). |
| `+0x174343` / `+0x17434A` | Copies the `maxPlayers` field from source to destination at `+0x9C`. |
| `+0x1764EB` / `+0x1764F0` | First observed settings-copy call / return address. |
| `+0x1764F4` | Calls settings validator `+0x177C40`. |
| `+0x17761A` / `+0x17761F` | Second observed settings-copy call / return address. |
| `+0x177C40` | Whole-settings validator. |
| `+0x177E65` | Loads `maxPlayers` from settings field `+0x9C`. |
| `+0x177E6B` | Lower-bound comparison for `maxPlayers`. |
| `+0x177E77` | Upper-bound comparison for `maxPlayers`. |
| `+0xACDFA0` | Vanilla minimum allowed `maxPlayers`: `1`. |
| `+0xACDFA4` | Vanilla maximum allowed `maxPlayers`: `64`; more-slots hook target. |

The range check is logically `1 <= settings.maxPlayers <= 64`. Do not patch
`+0xACDFA0` when trying to raise the maximum: that entry is the minimum.

`server/hooks/engine/hook_more_slots.cpp` raises only `+0xACDFA4`. A live test
with `128` confirmed that the validator saw field `+0x9C = 128`, returned true,
and the normal settings pipeline retained two copies containing `128`. The
server browser advertised 128 slots.

## Useful debugger cautions

- x64dbg hardware write breakpoints stop after the write, so the writer is
  normally the instruction immediately before the displayed RIP.
- x64dbg xref analysis missed the handler vtable reference. A direct PE scan
  recovered `+0x15CF16 -> +0x78DFC8`; absence from `analxrefs` is not proof that
  an address is unused.
- For the large consumer at `+0x15F870`, the observed return address was at
  `[rbp+0x7188]` because of its seven pushes and unusually large stack frame.

## Status & provenance

The tree-felling selector, the `TreeType 0`/`TreeType 7` values, and the apple →
chair override are runtime-verified. The gem and tunnelling call sites were
recovered statically and are byte-pattern-checked at attach time by
`hook_gem_drop.cpp` and `hook_tunnel_drop.cpp`, but the inferred signatures
called out above are not fully runtime-verified.

Two references in this page point at material that is **not yet in this
repository**: `more-slots.md` and `server/hooks/engine/hook_more_slots.cpp`.
They are retained as written so the analysis stays intact; port them before
relying on that section.
