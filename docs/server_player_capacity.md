---
title: Server player capacity — 128 slots and the 65th-entry overflow
author: Zbigi
assistance: ChatGPT (Codex)
status: re
domain: reverse-engineering
tags: [server, capacity, max-players, connections, movement]
related: [offsets.md, loader_and_injection.md]
sources: [docs/evidence/server_player_capacity_reference.json, docs/offsets.md, source/server/hooks/engine/hook_player_capacity.cpp, source/server/cm_server.cpp, scripts/test_player_capacity.cpp]
updated: 2026-11-12
---

# Server player capacity — 128 slots and the 65th-entry overflow

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

The optional Plus `playerCapacity` extension now raises **both** limits,
supporting a configured ceiling from `65` through `128`. It is a new Plus
implementation of the documented native boundaries; it does not copy the
external LiFXXL capacity implementation. The packaged option is disabled until
the operator enables it and requests the matching world capacity.

LiFXXL is a community server project with players. The
[evidence record](evidence/server_player_capacity_reference.json) includes
historical production-incident aggregates, exact reference instructions and
source fingerprints; it identifies the historical observations as such.

## Reference build

All addresses are server RVAs for Windows x64 `ddctd_cm_yo_server.exe` 1.4.4.5:
`12,441,032` bytes, SHA-256
`ACD99BD1A785D95B06494CBADE51A2C14EE60A521D0E88A4BE6AE1B3B685219F`.
Add the actual loaded module base under ASLR; preferred base `0x140000000`.

## Enable the Plus extension

In the deployed `config/lifxpluss.xml`, inside its existing root:

```xml
<playerCapacity enabled="1" slots="128" />
```

In the actual world's XML (for example `config/world_1.xml`), change its
existing setting:

```xml
<maxPlayers>128</maxPlayers>
```

`slots` raises the validation ceiling and allocates that many movement-buffer
entries. The world's `maxPlayers` still controls the requested limit and must
not exceed `slots`. A lower world setting remains valid. Missing/disabled
`playerCapacity` leaves the stock code unchanged; malformed enabled settings
stop startup. There is no live setter and no implicit edit of world XML.

Build the modified Plus DLL through the existing [build instructions](build.md)
and use the normal [loader](loader_and_injection.md). The implementation is
included in both the Windows project and the Linux cross-build source list.
The server remains a Windows x64 executable under Wine on Linux; this option
does not require a new client addon or an edited server EXE. Linux/Wine runtime
acceptance for this new implementation is still to be recorded.

At console startup, before world loading, expect:

```text
[playerCapacity] installed: validation ceiling=128, movement buffer=128; set world maxPlayers explicitly
```

If installation fails, Plus reports the reason and terminates startup instead
of continuing with an incomplete capacity patch. Reverting requires stopping
the process, disabling the option and restoring a world limit of at most `64`.
Do not unload/reload the DLL to change capacity while a world is running.

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

This contract is implemented in
[`hook_player_capacity.cpp`](../source/server/hooks/engine/hook_player_capacity.cpp).
The supported Plus section is `playerCapacity`; external `moreSlots` XML names
are not automatically recognized.

### Installation and allocation lifetime

The installer checks AMD64 PE metadata (`TimeDateStamp=0x6465EF7D`,
`SizeOfImage=0xC8F000`), both original bounds, a zero pre-simulation cursor,
and the complete `14`-byte reset, `65`-byte append/overflow and `30`-byte
validator blocks. These are startup compatibility checks, not a full-file
hash of the relocated in-memory image. Occupied or mismatched sites are rejected.

A zero-initialized, writable, non-executable allocation is obtained within
signed `rel32` reach. All four displacement values are checked before changing
the engine. All affected pages are made writable before any instruction,
cursor or ceiling is changed. The original append, cursor increment and
overflow instructions remain in place; only their buffer references change.
An instruction-cache failure rolls back the original bytes, ceiling and cursor
and causes startup to stop. Failure to restore page protection also stops
startup and retains any allocation still referenced by native code.

The installed allocation is intentionally owned by the **process lifetime**.
There is no live detach or buffer release from DLL teardown. Native instructions
reference the allocation directly, not a function or static array inside the
DLL. The OS releases it when the process exits. Installation assumes the normal
startup lifecycle; it is not a concurrent patching API.

This buffer counts eligible controlling-connection entries processed during a
tick, not Steam-list metadata. Server-local controlled actors also need to be
accounted for. Native overflow behavior is retained beyond the new buffer end;
an `N+1` entry is not silently accepted or written out of bounds.

## Executable-memory regression

[`scripts/test_player_capacity.cpp`](../scripts/test_player_capacity.cpp) executes
the reference reset, append/overflow and validation instruction blocks in a
synthetic image against fake connections. It includes the production installer.
It does not start or attach to a game process.

To reproduce from the repository root in an **x64 Native Tools Command Prompt
for VS 2022**:

```bat
if not exist bld\player-capacity mkdir bld\player-capacity
pushd bld\player-capacity
cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /DNOMINMAX /D_WIN32_WINNT=0x0A00 /D_CRT_SECURE_NO_WARNINGS /I ..\..\source /I ..\..\extra\include ..\..\scripts\test_player_capacity.cpp ..\..\source\core\tinyxml2.cpp /Fe:player_capacity_test.exe
if not errorlevel 1 player_capacity_test.exe
popd
```

- The unmodified block reproduces deletion of the first processed connection
  when the `65th` entry reaches the stock `64`-slot buffer.
- Installed capacities `65`, `96` and `128` preserve every admitted pointer
  over repeated ticks; no connection is deleted within the configured capacity.
- At `N+1`, the original delayed-delete call and slot-zero replacement remain
  intact; the cursor and a canary beyond the logical end show no extra write.
- The actual signed validator retains its minimum of `1` and rejects `N+1`.
- Every expected instruction byte is tested for mismatch; altered PE metadata,
  bounds, late installation, invalid config, allocation failure, page-preparation
  failure, cache-flush failure and protection-restoration failure are covered.

The MSVC x64 regression completed **5624 checks** with `/W4 /WX` on
2026-11-12. This establishes these native-block and installer behaviors; a
128-player game session is a separate acceptance step.

The full MSVC x64 Release DLL build also passed. Its existing empty furnace
extension table was changed from a zero-length C array (MSVC error `C2466`) to
a standard empty `std::initializer_list`; it still supplies no additional
recipes. Linux cross-build registration was checked, but the Linux toolchain
and Wine runtime were unavailable for execution on this host.

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

The contribution now includes the optional Plus implementation and native-block
regression. No new concurrent-player load trial, Linux/Wine runtime trial or
deployment has been performed. The earlier validator/listing observations and
historical production incidents retain their original scope and authorship.
