---
title: Sector handoff design
status: design
domain: design
tags: [sector-handoff, net-events, dispatcher, world-stitching, mysql]
related: [dispatcher_wire_format.md, net_events.md, netevent_abi.md, netevent_receive_path.md]
sources: [source/server/cm_offsets.h, docs/dispatcher_wire_format.md, docs/net_events.md, docs/netevent_abi.md, docs/netevent_receive_path.md]
updated: 2026-06-26
---

# Sector handoff design

`SectorHandoff` NetEvent — design for issue #50 (umbrella #45, cross-world
stitching).

## TL;DR

Moving a live character + its inventory from one `ddctd_cm_yo_server.exe`
world to a peer world needs a **brand-new NetEvent class**. Nothing in the
binary today carries a character payload: the `cmDispUnitManager` dispatcher
channel is **party/guild membership sync, not handoff** (opcodes only move
`(leader_id, member_id, extra_id)` uint32s), and `ServerUUIDEvent` carries
only a 16-byte server-identity UUID. So `SectorHandoff` lives alongside
`ServerUUIDEvent` in the NetClassRep registry, registers via
`NetClassRep::add` = `NETCLASSREP_ADD` (`0x418C40`, `FUN_140418C40`), and
implements its own `pack`/`unpack`/`process`. This page fixes the **wire
shape, state machine, DB-ownership rules, and DLL hook surface** so the
follow-on RE and the Rust daemon (#47) build to a stable contract. No code
yet.

Companion docs:
- `docs/dispatcher_wire_format.md` — what the existing server↔server protocol can and can't do.
- `docs/net_events.md` — the canonical NetEvent vtable slot map (`pack`/`unpack`/`process`).
- `docs/netevent_abi.md`, `docs/netevent_receive_path.md` — the registration ABI and the receive/dispatch path RE.
- Issue #47 — Rust dispatcher daemon (shared MySQL, DB-backed sector map, async handoff with pending flag).
- Issue #45 — the umbrella for cross-world stitching.

## 1. Why a new NetEvent

The existing `cmDispUnitManager` opcodes (#49) are a **party/guild
membership sync between worlds — not a generic character handoff.** This is
pinned to recovered string literals from
`engine/source/app/unit/dispunitmanager.cpp` compiled into the binary, so
the opcode names below are the engine's own (see
`docs/dispatcher_wire_format.md` for the full table):

| Opcode | Handler (RVA) | Meaning |
|--------|---------------|---------|
| `1`    | `inviteMember` (`0x3C9AA0`)       | invite member into party |
| `2`    | `delMember` (`0x3C90F0`)          | remove member from party |
| `9`    | `informServer` (`0x3C9FE0`)       | notify another world a unit is theirs |
| `0xA` (10) | `transferLeadership` (`0x3CAB70`) | promote new leader |
| `0xB` (11) | `setNewLeader` (`0x3CAAC0`)   | mark unit as new leader on its world |
| `0xE` (14) | `sendAllMembers` (`0x3CA9B0`) | bulk-resync (emits opcode `6` per member, `0xD` if empty) |

Outbound dispatch is `cmDispUnitManager::processServerRequest` @ RVA
`0x3CA570`; reply opcodes `3..0xd` are the mirror and mutate party/role
state on the Unit's embedded sub-struct. The relevant `Unit` offsets:

- `+0x1B88` — party-membership sub-struct.
- `+0x148`  — pending-leader id (inside the sub-struct).
- `+0x158`  — pending flag.
- `+0x1B44` — cached party id.

The opcode tail is always `(leader_id, member_id, extra_id)` — three
`uint32`s. **There is no inventory, position, character, or token payload in
the binary today.** Re-enabling or extending these opcodes does not get us
handoff; it only moves party-membership facts. (Earlier scoping for #47
assumed `parseDispatcherReply` was the inbound side of a transfer protocol
we could lean on — the opcode helpers and string literals refute that.)

`ServerUUIDEvent` is a **separate** server↔server identity handshake: a
`0x48`-byte NetEvent (UUID written at `+0x40`), sent by the factory
`ServerUUIDEvent::send` = `SERVERUUIDEVENT_SEND` (`0x4E7370`,
`FUN_1404E7370`), posted via the `NetConnection+0x1F8` sink (see §6). It
carries a 16-byte UUID and nothing else, and is not extensible without
breaking the wire.

Conclusion: neither channel is reusable. `SectorHandoff` lives **alongside**
`ServerUUIDEvent` in the NetClassRep registry, posted on the same
NetConnection (the server↔server connection established at world startup),
packed/unpacked by its own per-class vtable overrides. The dispatcher
protocol remains a useful **shape reference** for how YO already speaks
server-to-server (signed UUIDs, ID-range allocation, the Rust-daemon
target), but the payloads we need don't exist yet.

## 2. Wire shape

One canonical packed layout, version-tagged for future migration. All
multi-byte fields little-endian (matches Torque on-wire convention).

```
+--------+--------+--------------------------------------------------------+
| offset | size   | field                                                  |
+--------+--------+--------------------------------------------------------+
|  0x00  |   1    | proto_version  (=1)                                    |
|  0x01  |   1    | flags          (bit0 = mounted; bit1 = has_pet; …)     |
|  0x02  |   2    | reserved                                               |
|  0x04  |   4    | seq            (per-pair monotonic; daemon assigns)    |
|  0x08  |   8    | handoff_token  (random u64, idempotency key)           |
|  0x10  |  16    | src_server_uuid                                        |
|  0x20  |  16    | dst_server_uuid                                        |
|  0x30  |   4    | char_id        (global; matches `character.ID`)        |
|  0x34  |   4    | account_id     (FK to `account.ID`)                    |
|  0x38  |  12    | pos            (3× float32: x,y,z, world coords)       |
|  0x44  |   4    | yaw            (float32, radians)                       |
|  0x48  |   4    | hp_q16         (uint32 fixed-point ×65536)             |
|  0x4C  |   4    | stamina_q16                                            |
|  0x50  |   4    | hunger_q16                                             |
|  0x54  |   4    | thirst_q16                                             |
|  0x58  |   2    | n_effects      (uint16)                                |
|  0x5A  |   2    | n_actions      (uint16, queued ability/timer ids)      |
|  0x5C  |  …     | effects[]      (each = u32 spellId, u32 ms_remaining)  |
|   …    |  …     | actions[]      (each = u32 actionId, u32 ms_remaining) |
|   …    |   4    | inventory_anchor_oid  (movable_objects.ID of bag root) |
|   …    |   2    | n_equipment    (uint16)                                |
|   …    |  …     | equipment[]    (each = u8 slot, u32 oid)               |
|   …    |  32    | hmac_sha256    (daemon shared secret over [0x00..end]) |
+--------+--------+--------------------------------------------------------+
```

**Why the snapshot is references-not-contents for inventory.**
`movable_objects` rows live in MySQL and are globally addressable by OID.
The daemon's `*_server_id_ranges` machinery already prevents OID collision
across worlds (#49 evidence). So the SRC writes the rows, marks the bag
root with the DST's server_id, and the DST materializes by querying — no
N-kilobyte inventory blob on the wire.

**Why HMAC.** Worlds talk over an internal LAN today but the daemon is a
separable component (#47). Signing prevents a stray rogue world from
yanking characters from a peer it wasn't authorized to.

## 3. State machine

Six states on each side, mirrored. The daemon is the source of truth for
"who owns the character right now"; SRC/DST are obeyers.

```
                SRC                                  DST
                ───                                  ───
  IDLE → REQUEST  ── HandoffRequest ──▶  RECV
                                            │
                                            ▼
                   ◀── HandoffAck ─────  STAGE  (DB: reserve handoff_token)
  ACK
   │
   ▼
  FREEZE  (block player input,
           stop ticking effects,
           snapshot to wire payload)
   │
   ▼
  SHIP  ── HandoffPayload ────────────▶  MATERIALIZE
                                            │ (DB tx: claim char_id,
                                            │  update character.current_server_id,
                                            │  rebind inventory rows,
                                            │  spawn ghost, push effects)
                                            ▼
  CONFIRM ◀── HandoffComplete ────────  COMPLETE
   │
   ▼
  RELEASE  (DB: drop ghost row, free
            local OIDs back to range mgr)
                                                                fault edges:
   FREEZE  ── HandoffAbort  (DST RST) ─▶  thaw, restore input
   SHIP    ── timeout T1 ───────────────▶  rollback to FREEZE then thaw
   MATERIALIZE ── failure ─────────────▶  HandoffNack to SRC; SRC thaws
```

**Timeouts.** T1=5s (ship round-trip), T2=30s (overall handoff). On T2
expiry, daemon flips `pending_handoff=false` on the character row and
both sides clear. SRC retains the player.

**Recoverable vs. unrecoverable.** Anything before `MATERIALIZE` commits
is recoverable (SRC keeps authority). Anything after is committed —
`COMPLETE` must succeed, and a SRC crash post-commit just means the
player logs in on DST when they reconnect (which is what we want).

## 4. DB ownership rules

This is the question we have to get right or we get duplicate
characters / lost inventory.

| Row | Pre-handoff owner | Mid-handoff (between SRC SHIP and DST COMPLETE) | Post-handoff |
|-----|-------------------|-------------------------------------------------|--------------|
| `character` | SRC | **Daemon** (sets `pending_handoff_token`, `current_server_id=NULL`) | DST |
| `movable_objects` rows in inv | SRC's range | Daemon flips `ServerID` to DST's range in one tx | DST |
| `unmovable_objects` (placed deco, claimed land) | Whoever | **Unchanged** (these are tied to world geography, not character) | Same |
| `character_skills` etc. (player-owned, world-independent) | n/a (global) | Untouched | n/a |
| `containers` referenced by inventory | Move with the movable_objects rows | Daemon tx | DST |

**Schema additions needed** (out of scope for this design doc, list only):

- `character.current_server_id INT UNSIGNED NULL` — null while in flight.
- `character.pending_handoff_token BIGINT UNSIGNED NULL` — matches wire field.
- `handoff_log (token, src, dst, char_id, started_at, ended_at, outcome)` — operational visibility.

**Range manager interaction.** When the daemon flips
`movable_objects.ServerID` for a bag's contents, those OIDs are now in
DST's range. The existing `movable_objects_server_id_ranges` intersection
detection (already in stored procs per #47 evidence) covers this — we
piggyback on it rather than introducing parallel locking.

## 5. Replay + crash safety

- **`handoff_token` is the idempotency key.** DST stores it in
  `character.pending_handoff_token` during STAGE; a duplicate
  `HandoffPayload` with the same token is a no-op (return
  `HandoffComplete` from current state).
- **DST crash mid-STAGE.** Token is on disk, payload isn't. On restart,
  DST sees `pending_handoff_token` set, posts `HandoffNack(token)`,
  daemon clears + SRC thaws. T2 timeout covers the case where DST never
  comes back.
- **SRC crash mid-FREEZE.** No row state changed. On restart, daemon
  notices no `HandoffComplete` and flips the pending flag off. Player
  next login lands on SRC because that's still the
  `current_server_id`.
- **SRC crash post-SHIP, pre-CONFIRM.** Daemon arbitrates: if DST
  reached MATERIALIZE, DST is authoritative and SRC's stale ghost
  (which it doesn't know to drop) becomes a stray. Mitigation: on SRC
  restart, query daemon for any `outcome=committed` rows referencing
  this server as source and clean up.
- **Daemon crash.** Daemon state is durable in MySQL; on restart, scan
  `character WHERE pending_handoff_token IS NOT NULL` and resume or
  expire each.

## 6. Hook surface in the LiFx DLL

The class needs to register at engine init, the same way `ServerUUIDEvent`
does (the factory `SERVERUUIDEVENT_SEND` = `0x4E7370` / `FUN_1404E7370`
allocates a `0x48`-byte event and writes the vftable pointer into `*evt`).
Three pieces:

1. **NetEvent class registration.** A new `LifxRegisterSectorHandoffEvent`
   constructor lives in a `.CRT$XCU`-equivalent slot we inject from the
   DLL on attach. It statically constructs a `ClassRep` and calls
   **`NetClassRep::add` = `NETCLASSREP_ADD` (`0x418C40`, `FUN_140418C40`)**
   — a single-line head insert into the global classRep linked list (head
   at `DAT_140BC00B0`, next-ptr at `+0x50` on the rep). Every NetEvent /
   SimObject class self-registers here during static init; landing on the
   same list puts us in the same registry as `ServerUUIDEvent`. The
   `ClassRep` instance is `0x80` bytes of pure metadata (no function
   pointers) — full field layout in `docs/netevent_abi.md`.

   > **Correction (was imprecise in the original draft).** An earlier
   > version of this doc said registration "calls `NetClassRep::add` (same
   > fn used by all slots in the registry around `0x140733900`)". That
   > region is the **script-binding / namespace registry**
   > (`EngineFunctionInfo::add` = `FUN_14041DF20`, its own list head at
   > `0x140BC2140`, next-ptr `+0x80`), which is **distinct** from
   > `NetClassRep::add`. The correct NetClassRep registrar is
   > `NETCLASSREP_ADD` (`0x418C40`), confirmed in
   > `source/server/cm_offsets.h` and by the runtime dumper (#54/PR #55).

2. **Hook points in TS-callable space.**
   - `Server::onClientPrepareSectorLeave(%client, %destServer)` —
     fires when the player crosses a sector boundary heading toward a
     peer-managed sector. Returns the snapshot dict to the DLL, which
     packs and posts.
   - `Server::onClientSectorArrive(%client, %srcServer, %token)` —
     fires after DST MATERIALIZE completes; lets scripts re-attach UI,
     spawn pets, etc.
   - `Server::onClientSectorHandoffFailed(%client, %reason)` — fires on
     any fault edge that returns control to SRC; lets scripts unfreeze
     the player visually.

   These are new script callbacks. The DLL hook adds them to the same
   binding registry that `clientConnection.cs.dso` already targets, so
   modders can override without re-decompiling.

3. **NetConnection event post.** The DLL enqueues the event on the
   established server↔server NetConnection, the same path
   `ServerUUIDEvent::send` uses. See §6a for the corrected post-path
   mechanics.

### 6a. Post path — corrected (`NetConnection+0x1F8` is a sink, not a vtable slot)

The original draft (§6.3 / §7.2 below) framed the post as "the existing
vfn at `+0x1F8` on the NetConnection." That framing is **superseded** by
the ABI RE (`docs/netevent_abi.md`, `docs/netevent_receive_path.md`):

- `NetConnection+0x1F8` is **not** a NetConnection vtable slot. It is a
  pointer to an aggregated **event-sink / EventQueue subobject**. The
  factory dereferences `**(NetConnection+0x1F8)` to reach the sink's
  vftable and invokes **slot 0** of that vtable with `(sink, evt, …)`.
- The sink was captured at runtime (PR #55): its vtable is
  `0x140783118` — the GameConnection secondary-base subobject vtable.
  Slot 0 there is **`NetConnection::postNetEvent` = `FUN_140542CC0`**
  (attributed by the embedded `"NetConnection::postNetEvent"` string).
- `postNetEvent` is **enqueue only**: it refcounts the event and pushes a
  16-byte `{event_ptr, eventId, …}` record onto a per-connection queue at
  `this+0x5E8` (unordered) or `this+0x608` (ordered), selected by the
  event's `+0x14` refcount/reliability flag. Serialization happens later
  in the queue drainer `FUN_140542630` (identified via the
  `"[NED] EVT [%u](%s): SEND - Seq: %u LastAcked: %u"` string), which
  calls the event's per-class `pack`.
- Non-main-thread callers are wrapped in a `MainThreadEvent` and marshaled
  back to the main loop via `FUN_140427960`; the
  `"can't submit NetEvent [%d](%s) from non-main thread!"` log is a
  warning, not an error, so we needn't pin which thread posts.

None of these post-path RVAs are named constants in `cm_offsets.h` yet —
they live in `docs/netevent_receive_path.md`.

## 7. RE prerequisites — original ask, with current status

The three calling conventions this design depends on were listed as open
in the original draft. They have since been RE'd (#52/#54/#56/#87). The
original text is preserved verbatim; the resolution follows each item.

> 1. **`ServerUUIDEvent` vtable slots for `pack` and `unpack`.** Walk the
>    actual vtable of the 0x48-byte event (the qword stored at offset 0
>    by `FUN_1404E7370`) and find which slot indexes write a BitStream.
>    That tells us the calling convention (`(this, BitStream*,
>    NetConnection*)` vs alternatives) and the BitStream helper functions
>    (`writeInt`, `write`, `writeFlag`).
> 2. **`NetConnection::post` vfn at +0x1F8 — signature.** The factory
>    calls `(*(*conn[0x1F8]))(conn + 0x1F8, evt, ...)`. We need to know
>    what the trailing args are — they're probably `(reliable, priority,
>    guaranteed-ordering)` flags but Ghidra showed them as `param_3,
>    param_4`. RE'ing one caller of `NetEvent::send` from the *client*
>    binary side would disambiguate.
> 3. **`NetClassRep::add` signature.** Look at any of the script-binding
>    slots in `0x1407338B0..` (they all call `FUN_14041DF20` —
>    `EngineFunctionInfo::add`); the NetClassRep variant is similar but
>    distinct, and we want the exact arg order so registration doesn't
>    silently land in the wrong group.

**Resolution:**

1. **`pack`/`unpack` slots — resolved (and the "no per-class pack/unpack"
   hypothesis refuted).** See §7a below. Short version: pack/unpack **are**
   per-class vtable overrides — `pack` = primary `NetEvent` vftable
   **slot 2**, `unpack` = **slot 5**, `process` = **slot 7**, verified
   empirically across **129** NetEvent-derived classes
   (`docs/net_events.md`). On the MSVC secondary-inheritance subobject
   vftable the same methods appear at `pack` = slot 7, `process` = slot 13
   (`docs/netevent_receive_path.md`, #87); the send drainer and receive
   path reach the object through that secondary vftable. `ServerUUIDEvent`
   only *looked* generic because it carries no real payload (it inherits
   the stubs).
2. **Post path — resolved.** It is a **sink subobject**, not a NetConnection
   vtable slot; post via slot 0 of the sink (`postNetEvent` =
   `FUN_140542CC0`). See §6a.
3. **`NetClassRep::add` — resolved.** It is `NETCLASSREP_ADD` (`0x418C40`,
   `FUN_140418C40`), a plain head-insert (head `DAT_140BC00B0`, next `+0x50`),
   distinct from `EngineFunctionInfo::add` (`FUN_14041DF20`). See §6.1.

> Anything beyond those three is out of scope for the next RE pass —
> specifically, mapping the unit-membership sub-struct at `+0x1B88` is
> unrelated to SectorHandoff and can wait.

(That `+0x1B88` sub-struct is the dispatcher's **party-membership** block
from §1 — confirmed unrelated to handoff.)

### 7a. NetEvent serialization — runtime-built marshaler hypothesis (HISTORY) → per-class slot overrides (CURRENT)

This is the one claim that flipped during RE, so it is reconciled
explicitly.

**Historical conclusion (issues #52/#54, static-only pass — SUPERSEDED).**
A runtime dumper (#54 / PR #55) captured **247** ClassRep instances and
confirmed the ClassRep struct is **pure metadata** (no `EXEC`-tagged
qwords). Walking the **ClassRep vtable (9 slots)** showed only `setData`,
`getCategoryName`, inherited accessors, `create()`, `destroy` — no
pack/unpack. The walk of `ServerUUIDEvent`'s **event vtable** (symbol at
`0x1408A03E8`) stopped at **7 slots**, all generic, and `ClassRep+0x30` was
zero-initialized in `.data`. From that the pass inferred there were **no
per-class pack/unpack virtuals anywhere static** — i.e. that this Torque
variant used a **generic per-rep marshaler built at runtime** from
`(rep.size, rep.flags, rep.namespace)`. That negative result drove this
design's core decision: **don't try to mirror an existing class's
pack/unpack — implement our own and RE the receive path separately.**

**Current finding (issues #56/#87, `docs/net_events.md`,
`docs/netevent_receive_path.md` — CORRECTS THE ABOVE).** Per-class
pack/unpack **do** exist as vtable overrides; the earlier walks looked at
an incomplete view (the 7-slot `ServerUUIDEvent` event-vftable symbol and
the 9 visible ClassRep slots). Empirically, NetEvent-derived classes have
**13 virtual slots** with these per-class overrides on the primary
vftable: **`pack` = slot 2, `unpack` = slot 5, `process` = slot 7**
(measured by counting distinct targets at each slot across 129 classes —
~128 distinct ⇒ per-class override). The "generic byte-copy marshaler"
framing is **refuted**: serialization is per-class. `ServerUUIDEvent`
appeared generic because the YO build sends **no UUID bytes** — its
receive-side `unpack` resolves to the inherited stub
`FUN_1400A9A50 → FUN_140418710`, which is a *SceneObject-lineage check*,
not a deserializer.

**What both agree on (the design conclusion is unchanged):** we cannot
reuse an existing class. `SectorHandoff` must define **its own** `pack` /
`unpack` / `process` overrides with direct BitStream calls and register via
`NetClassRep::add`. The receive path had to be RE'd separately — which #56
did (§7b). So the special instruction's premise ("needs a NEW NetEvent and
a separately-RE'd receive path") holds; only the "no per-class vtable
entries" rationale was a transient mis-call now corrected.

### 7b. Receive / dispatch path — RE'd (issue #56)

The inbound path is `NetConnection::eventReadPacket` = `FUN_140541720`
(attributed by `"NetConnection::eventReadPacket: hasLastError() before
readClassId"`):

1. `classId = BitStream::readInt(stream, 0)` = `FUN_140448580`; reject
   `0xFFFFFFFF` ("bad event class id").
2. `rep = NetClassRep::findClass(0, classId)` = `FUN_140417770` — walks
   the global classRep list (head `module_base + 0xBC00B0` =
   `DAT_140BC00B0`, next `+0x50`), matching `group` and the `classId`
   stored at `rep+0x08`. Group `0` = NetEvent.
3. Direction enforcement: `rep->v[5]()` returns `&repFields`; `+0x3C` is
   direction (`1`=server-only, `2`=client-only, `0`=both).
4. Per-class **unpack** — ClassRep vtable **slot 8** override:
   `(*rep->v[8])(rep, conn, stream)`. For `ServerUUIDEvent` this is the
   inherited stub (no payload). Classes WITH payload override slot 8 with a
   real reader.
5. **Length-match sentinel:** `pack` writes `(classId XOR 0xf00dbaad)` at
   the end of the payload; `unpack` verifies it. Mismatch ⇒ `"unpack did
   not match pack for event of class %s"`.
6. Schedule `process` on the main thread.

> **Slot-number caveat (two vftables, MSVC multiple inheritance).** There
> are two slot maps in play and they are not contradictory: the **primary**
> `NetEvent`/ConsoleObject-layout vftable has `pack`=2, `unpack`=5,
> `process`=7 (`docs/net_events.md`); the **secondary subobject** vftable
> the send drainer/receive path reach through has `pack`=7, `process`=13
> (`docs/netevent_receive_path.md` #87), and the receive code dispatches
> **unpack via the ClassRep vtable slot 8** override. When implementing,
> pin the slot against the exact vftable you are cloning; both coexist on
> the same object.

These receive-path RVAs (`FUN_140541720`, `FUN_140417770`, `FUN_140448580`,
`FUN_140542630`, sink vtable `0x140783118`, sentinel `0xf00dbaad`) are
documented in `docs/netevent_receive_path.md` and are **not** named
constants in `source/server/cm_offsets.h`.

### 7c. Implementation recipe (consolidated)

1. **Define `SectorHandoffEvent`** as a C++ class derived from NetEvent;
   clone the vftable layout of the secondary subobject discovered in #54.
2. **Construct a custom `ClassRep<SectorHandoffEvent>`** (`0x80` bytes per
   `docs/netevent_abi.md`) in the DLL's static data; `+0x00` points at our
   own 9-slot ClassRep vtable, `+0x40` = instance size, `+0x3C` direction =
   `0` (bidirectional). The engine assigns the runtime classId via the
   linked-list walk, so we likely need not set `+0x08` ourselves.
3. **Override `unpack`** (ClassRep slot 8) with a real BitStream reader that
   populates the event struct per §2; override **`pack`** (secondary
   vftable slot 7) to write the same bytes plus the `classId XOR 0xf00dbaad`
   sentinel; override **`process`** (secondary slot 13 / primary slot 7) to
   run the §3 state-machine transition; override `create()` (ClassRep slot
   7) as `operator_new(size) + *evt = our event vftable + base init`
   (mirrors `FUN_1404E5CE0`).
4. **Register** via `NetClassRep::add` (`NETCLASSREP_ADD`, `0x418C40`) at
   attach.
5. **Send** by filling the event struct and calling `postNetEvent`
   (`FUN_140542CC0`) on the server↔server NetConnection's sink; the drainer
   `FUN_140542630` invokes our `pack`.

Still open before writing real `pack`/`unpack`: confirm the event vftable
`slot 10` classId-write step on the send drainer (a pad-stub returning 0
makes a real receiver reject with "Invalid packet. (bad event class id)").

## 8. Non-goals (explicitly)

- No daemon Rust code in this issue.
- No DB migration SQL.
- No DLL hook implementation.
- No NetEvent class registration hook.
- No TorqueScript-side player cap changes (#43).

## Status & provenance

**Design proposal** — no code shipped. The wire format (§2), state machine
(§3), DB-ownership rules (§4), and crash-safety model (§5) are unverified
design; they are the contract follow-on work builds against.

**Runtime-verified (RE):**
- `NETCLASSREP_ADD = 0x418C40` (`NetClassRep::add` / `FUN_140418C40`),
  global classRep list head `DAT_140BC00B0`, next-ptr `+0x50` — named in
  `source/server/cm_offsets.h`; confirmed by the 247-rep runtime dump
  (#54/PR #55).
- `SERVERUUIDEVENT_SEND = 0x4E7370` (`FUN_1404E7370`), `0x48`-byte event,
  UUID at `+0x40`, post via `NetConnection+0x1F8` sink — named in
  `cm_offsets.h`.
- Post-path sink vtable `0x140783118` and `postNetEvent` = `FUN_140542CC0`
  captured at runtime (PR #55).
- NetEvent slot map `pack`=2 / `unpack`=5 / `process`=7 — empirical across
  129 classes (`docs/net_events.md`).

**Reverse-engineered (static, not all runtime-verified):**
- Dispatcher opcode table and `Unit` offsets `+0x1B88` / `+0x148` /
  `+0x158` / `+0x1B44`, `processServerRequest` @ `0x3CA570` — from
  `engine/source/app/unit/dispunitmanager.cpp` string literals
  (`docs/dispatcher_wire_format.md`). **Settled negative result:** the
  dispatcher is party/guild sync, not character handoff.
- Receive path `eventReadPacket` `FUN_140541720`, `findClass`
  `FUN_140417770`, `readInt` `FUN_140448580`, drainer `FUN_140542630`,
  sentinel `0xf00dbaad` — `docs/netevent_receive_path.md` (#56/#87); not
  named in `cm_offsets.h`.

**Superseded but kept for history:**
- "Registration calls `NetClassRep::add` around `0x140733900`" — corrected
  to `0x418C40`; `0x733900` is the script-binding/namespace registry
  (`EngineFunctionInfo::add` = `FUN_14041DF20`, head `0x140BC2140`,
  next `+0x80`). See §6.1.
- "`NetConnection+0x1F8` is a vfn slot" — corrected: it is an event-sink
  subobject pointer; post = slot 0 of the sink. See §6a.
- "No per-class pack/unpack; serialization is a generic runtime marshaler"
  — corrected: per-class pack/unpack are vtable-slot overrides (slot 2/5
  primary). See §7a. The design conclusion it produced (new NetEvent +
  separately-RE'd receive path) still stands.

These reconciliations are intentional history markers, not live guidance —
follow §6.1, §6a, §7a–7c for the current ABI.
