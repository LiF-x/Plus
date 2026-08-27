---
title: Dispatcher wire format
status: re
domain: reverse-engineering
tags: [dispatcher, net-event, party-sync, server-to-server, wire-format]
related: [net_events.md, netevent_abi.md, netevent_receive_path.md, sector_handoff_design.md]
sources: [source/server/cm_offsets.h, scripts/ghidra/LifxDspWireFormat.java, scripts/ghidra/LifxDspWireFormat2.java, net_events.md, netevent_abi.md, sector_handoff_design.md]
updated: 2026-06-26
---

# YO dispatcher wire format (P0 RE)

**TL;DR.** The YO server already speaks a server↔server protocol over the
standard Torque `NetConnection`, but it is narrower than its name suggests:
the `cmDispUnitManager` opcode channel is a **party / guild membership sync**
layer (invite / kick / promote / resync a *Unit*'s party state across worlds),
**not** a character or object handoff. It carries only three `uint32` IDs per
message — no position, no inventory, no token. A separate one-shot
`ServerUUIDEvent` handshake establishes peer identity (a 16-byte UUID). Sector
handoff (#45/#50) therefore **cannot** piggyback on `parseDispatcherReply`; it
needs a new NetEvent class layered on the same NetConnection / `NetEvent::pack`
infrastructure (see [`sector_handoff_design.md`](sector_handoff_design.md)).

Source: headless RE of `ddctd_cm_yo_server.exe`, driven by
`scripts/ghidra/LifxDspWireFormat.java` and `LifxDspWireFormat2.java`. The
opcode-name attributions below are pinned to **recovered string literals from
`engine/source/app/unit/dispunitmanager.cpp`** (compiled into the binary), so
`inviteMember`, `delMember`, `informServer`, `transferLeadership`,
`setNewLeader`, `sendAllMembers` are the engine's own names, not guesses.
Tracks issue #48; informs #47 (Rust daemon) and #45 (sector handoff).

> All RVAs are relative to image base `0x140000000`. Only `0x4E7370` is
> currently a named constant in `cm_offsets.h` (`SERVERUUIDEVENT_SEND`,
> `cm_offsets.h:334`); every other dispatcher RVA below is a raw Ghidra
> address not yet promoted to a `CmOffset`.

## Transports

### `ServerUUIDEvent` (server↔server identity)

| | |
|---|---|
| Factory       | `FUN_1404e7370` @ RVA `0x4E7370` — named `SERVERUUIDEVENT_SEND` in `cm_offsets.h`; the script-binding fn at `0x4E7260` / `0x4E7760` is a thin shim |
| Object size   | `operator_new(0x48)` |
| UUID slot     | `+0x40` populated from `FUN_1404FC230(DAT_140B7D3A8)` (process-wide UUID singleton) |
| Send call     | `(*(*conn[0x1F8]))(conn + 0x1F8, evt, ...)` — i.e. the NetConnection-resident event posting vfn at offset `0x1F8` |
| Trigger       | TorqueScript `SendServerUUIDEvent([target])` (broadcast if no arg) |

Wire shape on the line is whatever the Torque `NetEvent::pack` slot for this
class writes; the only event-specific payload is the 16-byte UUID at `+0x40`.
The generic NetEvent send path is documented in
[`netevent_receive_path.md`](netevent_receive_path.md) and the per-class
vtable slots (`pack`=2, `unpack`=5, `process`=7) in
[`net_events.md`](net_events.md).

### `cmDispUnitManager` request/reply (party/guild)

Two halves, both keyed by a 1-byte opcode followed by `(uint32 a, uint32 b,
uint32 c)` IDs. IDs are unit IDs resolved via `FUN_140134850(id, allow_party)`
returning a `Unit*`.

#### Outbound — `cmDispUnitManager::processServerRequest` @ RVA `0x3CA570`

| Opcode | Handler                              | Meaning                              | Payload tail used |
|-------:|--------------------------------------|--------------------------------------|------------------|
| `1`    | `inviteMember` (`0x3C9AA0`)          | invite member into party             | leader, member   |
| `2`    | `delMember`    (`0x3C90F0`)          | remove member from party             | leader, member, kicked-by-leader, voluntary flags |
| `9`    | `informServer` (`0x3C9FE0`)          | notify another world a unit is theirs| leader, member, leader-flag |
| `0xA`  | `transferLeadership` (`0x3CAB70`)    | promote new leader                   | old, new         |
| `0xB`  | `setNewLeader` (`0x3CAAC0`)          | mark unit as new leader on its world | leader, member, role |
| `0xE`  | `sendAllMembers` (`0x3CA9B0`)        | bulk-resync (emits opcode 6 per member, opcode 0xD if empty) | unit |

(`0xA` = 10 decimal, `0xB` = 11, `0xE` = 14 — the engine source uses the
decimal forms `10=transferLeadership`, `0xb=setNewLeader`, `0xe=sendAllMembers`.)

All outbound sends ultimately flow through `FUN_1403CC860(DAT_140B846D8,
op, a, b, c)` — i.e. they re-enter the same handler shown below on the
*peer* server.

#### Inbound — `cmUnitManager::parseDispatcherReply` @ RVA `0x3CC860`

Signature: `parseDispatcherReply(self, uint32 op, uint32 idA, uint32 idB, uint32 extra)`.

Two ID lookups are done up front:

```c
plVar3 = (Unit*)FUN_140134850(idA, /*include_party=*/1);
plVar4 = (Unit*)FUN_140134850(idB, /*include_party=*/1);
```

The membership sub-struct lives at `unit + 0x1B88`; the cached party id is at
`unit + 0x1B44`. Three predicates on the sub-struct (`+0x148` = pending
leader id, `+0x158` = pending flag) are used throughout:

- `FUN_1403CBA30(m)` → has pending member-add (`m->148 != 0 && m->158 == 0`)
- `FUN_1403CB5E0(m, id)` → pending member-add matches `id`
- `FUN_1403CBA10(m)` → has pending member-add already confirmed (`158 != 0`)

| Opcode | Action |
|-------:|--------|
| `3`    | A: clear membership; signal `0x2DE` on UUID(A) |
| `4`    | A: same as 3, then `0x3CC430` (full detach: clear party + sub-struct) |
| `5`    | B: if pending member-add(A) is in good state → call `_doPromote` (`0x3CC770`); always clear pending (`0x3CB890`); signal `0x2DD` |
| `6`    | A: add party-row(extra, role=3); if connected, fire signal `0xB23` on UUID(extra) with name(A) |
| `7`    | B: mirror of 5 but for "not-yet pending" path |
| `8`    | A,B: signal `0x61` on UUID(A) with name(B); then add party row (`0x3CC470`, idB, extra) |
| `9`    | A: add party row (`0x3CC470`, A, extra) |
| `0xA`  | B: cancel pending leader-promotion on B (idA), restore cached party id on B; fall through to `processServerRequest(9, 0, idB, 0, 0)` |
| `0xB`  | B: set pending member-id on B's sub-struct to `extra` |
| `0xC`  | B: same shape as 5 but the pending check is inverted |
| `0xD`  | A: full detach (mirror of opcode 4 second leg) |
| other  | logs `"cmUnitManager::parseDispatcherReply(%u, %u, %u) -- bad EventType!"` |

The opcodes form a request/ack matrix: outbound 1→ inbound 3/4, outbound 2→
inbound 5/7/0xC, outbound 9→ inbound 6/8/9, outbound 0xA→ inbound 0xA/0xB,
outbound 0xE→ inbound 6 (per member) / 0xD (empty).

## DspUtil lookup (`0x5367A0`)

Both halves resolve `unitId → ConnectionEntry*` via
`FUN_1405367A0(DAT_140BF1A60, id)`. It is an open-addressed hash keyed by
the 4-byte id using FNV-1a–style mixing (`*0xCBF29CE484222325`, mul
`0x100000001B3`, final mul `0xCAEE32A7D4F6A63`). The bucket array is at
`DspUtil + 0xE8`; size mask at `DspUtil + 0x100`. Entry layout used by
callers:

- `+0x00` (`uint32`) — unit id
- `+0x2C` (`uint32`) — current party-leader id
- `+0x50` → `NetConnection**` (used for `processServerRequest` side-effects)

`FUN_1405368E0` checks "is connected" (non-null `+0x50` and `**(+0x50) != 0`).

## Signals fired (for handlers on the receiving world)

| ID | Where fired | Likely meaning |
|---:|-------------|----------------|
| `0x61`  | inbound op 8                  | party-member added |
| `0x2DD` | inbound op 5                  | member-add accepted |
| `0x2DE` | inbound op 3, 4               | member-removed |
| `0xB22` | inbound op 0xA                | leadership-transfer cancelled |
| `0xB23` | inbound op 6                  | new member joined party |

These are dispatched through `FUN_14008CC10` (Signal::trigger). A Rust daemon
that only forwards opcodes does not need to know these — they are world-local
fan-out.

## What this rules out

- **No character / inventory / position payload** is carried by these opcodes.
  The opcode tail is `(leader_id, member_id, extra_id)` — three `uint32`s,
  nothing more. Sector handoff (#45) cannot piggyback on
  `parseDispatcherReply`; it needs a new NetEvent class, registered alongside
  `ServerUUIDEvent` in the NetClassRep registry. The existing doc anchored the
  ServerUUIDEvent rep region at `0x140733900..0x140733948`; for the canonical
  registration ABI (`NetClassRep::add` = `NETCLASSREP_ADD` @ `0x418C40`, list
  head at `0x140BC00B0`, next-ptr at rep `+0x50`) see
  [`netevent_abi.md`](netevent_abi.md).
- The `0x1407338B0`-anchored table is the **script-binding registry**
  (`EngineFunctionInfo::add` slots), not a NetClassRep vtable — it is not on
  the dispatcher path.

## Reconciliation: dispatcher ≠ character handoff

Earlier scoping for issue #47 assumed `parseDispatcherReply` was the inbound
side of a transfer protocol we could lean on for sector handoff (#45). **That
assumption is superseded.** The recovered string literals and opcode helpers
(from `engine/source/app/unit/dispunitmanager.cpp`) show the channel only
moves party/guild membership facts between worlds — invite, kick, promote,
inform-server, bulk-resync — and mutates a *Unit*'s embedded party sub-struct
(`+0x1B88`). There is no inventory, position, or character payload in the
binary today.

The dispatcher protocol remains a useful **shape reference** for how YO
already does server-to-server (Torque NetConnection, signed/handshaked UUIDs,
unit-ID-keyed routing) — but #45 must design a new `SectorHandoff` NetEvent
on top, not re-enable these opcodes.

## Open RE follow-ups (out of scope for #48)

- Map `unit + 0x1B88` membership sub-struct fields by name (likely
  `cmUnitParty`).
- Map the NetEvent `pack/unpack` slot pair on `ServerUUIDEvent`'s vtable so we
  can lock down byte ordering. Today only the construction is RE'd. (Per
  [`net_events.md`](net_events.md) the slots are `pack`=2 / `unpack`=5 /
  `process`=7; the per-class targets still need a runtime dump.)
- Identify the `FUN_1403CC860` outbound entry's transport — whether it goes
  via the same NetConnection vfn used by `ServerUUIDEvent::send` or a
  separate engine queue.

## Status & provenance

- **Runtime-verified:** none of the byte-order specifics. `ServerUUIDEvent`'s
  on-wire layout is only RE'd at *construction* time (0x48-byte alloc, UUID at
  `+0x40`); the `pack`/`unpack` slots that decide field order have not been
  dumped at runtime.
- **Reverse-engineered (static):** all RVAs, the opcode tables, the membership
  sub-struct offsets (`+0x148`/`+0x158`/`+0x1B44`/`+0x1B88`), the DspUtil hash
  layout, and the signal IDs — recovered via Ghidra
  (`LifxDspWireFormat.java`, `LifxDspWireFormat2.java`) and the
  `dispunitmanager.cpp` string literals compiled into the binary.
- **Cross-checked against `cm_offsets.h`:** `0x4E7370` is the named constant
  `SERVERUUIDEVENT_SEND` (`cm_offsets.h:334`); `NetClassRep::add` is
  `NETCLASSREP_ADD` (`0x418C40`). No conflicts found. Every other dispatcher
  RVA on this page is a raw Ghidra address with no `CmOffset` constant yet.
- **Inference (labelled "Likely"):** the human-readable meaning of the signal
  IDs in the Signals table.
