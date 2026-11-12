---
title: Server player capacity — the 64-player limit and the 65th connection
author: Zbigi
assistance: ChatGPT (Codex)
status: re
domain: reverse-engineering
tags: [server, capacity, max-players, connections, movement]
related: [offsets.md, loader_and_injection.md]
sources: [docs/evidence/server_player_capacity_reference.json, docs/offsets.md]
updated: 2026-11-11
---

# Server player capacity — the 64-player limit and the 65th connection

These documentation updates were generated with help from ChatGPT (Codex),
in collaboration with Zbigi, from notes and analyses collected over four months
of work on LiFXXL, a community project based on a modified LiF:YO server.

The earlier settings-parser and validator work is credited to **Pabluuz** in
the [offset encyclopedia](offsets.md#server-settings-and-player-slot-limit).

The reference server has two distinct constraints: a `maxPlayers` configuration
ceiling of 64 and a movement-processing buffer of 64 connection pointers.
Raising the first can advertise 128 slots while the second still removes a
connection when entry 65 is processed in a tick. This is a **64-entry capacity
reached by entry 65**, not an independent configuration limit of 65.

LiFXXL is a community server project with players. The
[evidence record](evidence/server_player_capacity_reference.json) includes
historical production-incident aggregates, exact reference instructions and
source fingerprints; it identifies the historical observations as such.

## Reference build

All addresses are server RVAs for Windows x64 `ddctd_cm_yo_server.exe` 1.4.4.5:
`12,441,032` bytes, SHA-256
`ACD99BD1A785D95B06494CBADE51A2C14EE60A521D0E88A4BE6AE1B3B685219F`.
Add the actual loaded module base under ASLR; preferred base `0x140000000`.

## Configuration acceptance

The world requests capacity through `<maxPlayers>128</maxPlayers>`.
The integer getter at `0x45ADB0` is called at `0x176E9B`; its result is stored
at settings `+0x9C` by `0x176EA0`. The settings copy at
`0x174343..0x17434F` propagates that field normally.

The validator at `0x177E65..0x177E82` checks signed bounds:

```text
minimum at 0xACDFA0 = 1
maximum at 0xACDFA4 = 64
accept only minimum <= settings.maxPlayers <= maximum
```

Changing the minimum is the wrong operation. Forcing a temporary settings copy
or returning success from the whole validator also bypasses other validation
without addressing the movement buffer.

**Address correction:** the `maxPlayers` string is at `0x792830`.
The former `0x793830` entry in `offsets.md` is a transcription error for this
executable. The RIP-relative load at `0x176E8A` and string bytes agree on
`0x792830`; the evidence records bytes at both locations.

## The separate per-tick limit

`ProcessList::advanceObjects` resets the connection-buffer cursor each tick.
The movement callback appends the controlling connection for eligible controlled
objects. The relevant state is:

| RVA | Meaning |
| --- | --- |
| `0xB79FB0` | First connection-pointer slot. |
| `0xB7A1B0` | Exclusive end of the original buffer. |
| `0xB79BC8` | Current write cursor. |
| `0x13A55C` | Loads the buffer start for the tick reset. |
| `0x13B98C` | Loads the exclusive end for admission. |
| `0x13B9AC` | Reads slot zero on overflow. |
| `0x13B9C0` | Calls virtual slot `+0x130` with delay argument `1`. |
| `0x13B9C6` | Replaces slot zero with the current connection. |

`(0xB7A1B0 - 0xB79FB0) / 8 = 64` pointer entries. The native block is:

```text
if cursor < end:
    *cursor = current_connection
    cursor += sizeof(pointer)
else:
    if buffer[0] != null:
        buffer[0].safeDeleteObject(1)
    buffer[0] = current_connection
```

GameConnection's reference vtable at `0x782DA8`, slot `+0x130`, resolves to
`SimObject::safeDeleteObject` at `0x430690`. Overflow can remove an existing
connection; it is not just an omitted array write. The victim is the first
connection processed into the buffer during that tick, which need not be the
oldest login or the connection processed 65th.

## What a larger-capacity implementation must cover

For requested capacity `N`, keep the world's `maxPlayers`, validation ceiling
and movement storage capacity consistent. The LiFXXL source reference allocates
`N * sizeof(void*)` storage and redirects four references together:

| Native instruction | Required target |
| --- | --- |
| `0x13A55C` | New buffer start. |
| `0x13B98C` | New start + `N * 8`, exclusive end. |
| `0x13B9AC` | New first slot for overflow reads. |
| `0x13B9C6` | New first slot for overflow writes. |

Changing only the end permits writes past the original allocation. Changing only
the reset pointer leaves the comparison and overflow path on the old allocation.
Suppressing deletion does not create storage for omitted connections.

An injected implementation must verify the executable and complete instruction
blocks, allocate storage before installing references, satisfy signed 32-bit
RIP-relative reach and keep the storage alive until no native instruction can
reference it. Installation/removal belongs at startup/shutdown, not during
active play. Preserve the cursor, movement work and overflow behavior at the
new configured ceiling.

This is an implementation contract, not a new Plus `moreSlots` option.
LiFXXL's external XML names are not automatically recognized by
`game/config/lifxpluss.xml`. Use the injected extension path documented in
[Loader and injection](loader_and_injection.md); do not distribute edited EXEs.

## Advertised slots and active players

The LiFXXL production incident analysis dated 2026-09-05 recorded:

| Existing world characters before spawn | Attempts | Empty-reason disconnect within one second |
| --- | ---: | ---: |
| 0–63 | 123 | 0 |
| 64 | 92 | 92 |

These historical log aggregates explain why advertised capacity and preconnected
sessions above 64 did not establish sustained world occupancy above 64. They
are not new measurements of the current deployment.

To accept a larger-capacity build, record its identity and loaded capacity,
then establish sustained world presence and movement across 63, 64, 65 and the
intended target. Include reconnects above occupancy 64 and confirm that existing
players remain connected. Measure tick time, memory, networking and gameplay
with the actual population and map.

Removing these two confirmed constraints is necessary for this implementation;
it does not establish unlimited capacity or prove every subsystem supports an
arbitrary integer. Listing metadata alone is not that proof.

## Status & provenance

The review checked five complete native instruction ranges, both validator
bounds, the virtual-call target and corrected string address against the
reference executable on 2026-11-11. Historical production aggregates retain
their source/date in the evidence; individual player identifiers are omitted.

This Plus contribution changes documentation. It does not import the capacity
implementation or claim a new concurrent-player load trial. The earlier
validator/listing observations retain their original scope and authorship.
