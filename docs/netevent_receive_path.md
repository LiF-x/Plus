---
title: NetEvent receive / send path
status: re
domain: reverse-engineering
tags: [netevent, receive-path, dispatch, torque]
related: [net_events.md, netevent_abi.md, sector_handoff_design.md]
updated: 2026-06-26
---

# NetEvent receive / send path (issue #56)

Companion to `docs/netevent_abi.md` (#54). Pinned by the runtime-dumper
sink capture from PR #55 (`sink @ 0x140783118` = GameConnection
secondary-base subobject vtable) plus a static string-xref pass over
`engine\source\sim\netevent.cpp` and `netconnection.cpp`.

Scripts: `scripts/ghidra/LifxNetEventPostVfn.java`,
`LifxNetEventCppRefs.java`.

## Confirmed call chain

### Send: `NetConnection::postNetEvent` = `FUN_140542CC0`

Slot 0 of the GameConnection-subobject vtable at `0x140783118`. The
function string `"NetConnection::postNetEvent"` is embedded in its
error path, so attribution is certain.

It is **only the enqueue** — refcounts the event, pushes a
`{event_ptr, eventId, ...}` 16-byte record onto a per-connection
queue at `this + 0x5E8` (unordered) or `this + 0x608` (ordered),
selected by `event->refcount_flag` at `+0x14`. The actual
serialization happens in the queue drainer.

A non-main-thread caller wraps the event in
`MainThreadEvent<lambda, NetConnection*, StrongRefPtr<NetEvent>>` and
hands it to `FUN_140427960` (the thread-marshal), which posts it back
into the main loop.

### Drainer / send: `FUN_140542630`

Walks the per-connection queue and emits bytes for each event.
Identified via the `"[NED] EVT [%u](%s): SEND - Seq: %u LastAcked: %u"`
debug string. Vtable calls on the event/rep observed:

- `(*v[5])(rep)` — `getClassRep()`, returns the rep (we already know).
- `(*v[10])(rep, conn)` — likely the pre-pack writeClassId step.
- `(*v[11])(rep, conn, 0)` — the pack call (see "Whole-event byte
  serialization?" below).
- `(*v[12])(rep, flag)` — direction / guaranteed-delivery accessor.

Slot indices are from the ClassRep vtable (we mapped 9 slots in
docs/netevent_abi.md; slots 10/11/12 fall past the visible end —
the ClassRep vtable extends further than the 9 slots we walked, OR
these calls go through a secondary subobject vtable spliced into
the same .rdata block).

### Receive: `NetConnection::eventReadPacket` = `FUN_140541720`

Attributed by the error string
`"NetConnection::eventReadPacket: hasLastError() before readClassId"`.

```c
// 1. Read 32-bit class ID from the wire
classId = BitStream::readInt(stream, 0);              // FUN_140448580
if (classId == 0xFFFFFFFF) { error("bad event class id"); return; }

// 2. Look up the rep — first arg is the class group (0 = NetEvent)
rep = NetClassRep::findClass(0, classId);             // FUN_140417770
if (!rep) { error("Invalid packet. (bad event class id: %d)"); return; }

// 3. Direction enforcement
//    rep->v[5]() returns &repFields; +0x3C = direction
//      1 = server-only, 2 = client-only, 0 = both
classRep = (*rep->v[5])(rep);
if (classRep->dirField != 0 && wrongDirection(conn)) error;

// 4. Per-class unpack — slot 8 on the ClassRep vtable
//    For ServerUUIDEvent this resolves to FUN_1400A9A50 ->
//    FUN_140418710 (the generic unpack thunk; see below).
(*rep->v[8])(rep, conn, stream);

// 5. Length-match sentinel: pack writes (classId XOR 0xf00dbaad)
//    at the end of its payload; unpack verifies the same after reading.
//    Mismatch -> "unpack did not match pack for event of class %s"
sentinel = readMagic(stream);
if ((sentinel ^ 0xf00dbaad) != classId) error;

// 6. Schedule processing on the main thread
process(rep);
```

### NetClassRep::findClass = `FUN_140417770`

`(group, classId) -> ClassRep*`. Walks the global classRep list
(head at `module_base + 0xBC00B0` per PR #55, next-ptr at
`+0x50`) looking for a rep whose group matches `group` and whose
classId matches `classId`. ClassId is stored at `rep + 0x08` (the
"classIdx" field documented in `docs/netevent_abi.md`).

## ClassRep slot 8 = per-class unpack (via override)

ClassRep vtable slot 8 for ServerUUIDEvent's rep is `FUN_1400A9A50`,
which calls `FUN_140418710`. Decompile of `FUN_140418710` shows it
does **not** serialize — it walks a parent-class chain looking for
the string `"SceneObject"`:

```c
void FUN_140418710(rep) {
    sceneObj = stringTableLookup("SceneObject");
    for (; rep && rep[0x48] != sceneObj; rep = rep[0x58]) { }
}
```

That's a "type-check whether this rep's lineage includes SceneObject"
predicate, not unpack. Two consequences:

1. **ServerUUIDEvent's rep doesn't override slot 8 with real unpack
   code** — it inherits the generic stub. The event's 16-byte UUID
   payload is therefore handled by some path we haven't yet traced
   (most likely the post-vtable-call section of `eventReadPacket`
   which reads the `magic ^ classId` sentinel and quietly succeeds
   because nothing was actually written/read for the payload either
   — i.e. the UUID is **not** sent on the wire by the YO build at
   all).
2. **Reps for events that DO have wire payload override slot 8** with
   a class-specific reader that pulls bytes off the BitStream into
   the event struct. We haven't located such a rep; needs a sweep
   for any ClassRep whose vtable slot 8 differs from `FUN_1400A9A50`.

The byte-copy hypothesis from the earlier draft of this doc is
**refuted**. Serialization is per-class via slot-8 override, not
automatic.

## What this means for SectorHandoff (#45 / #50)

The implementation recipe:

1. **Define `SectorHandoffEvent`** as a C++ class derived from
   NetEvent (concrete base TBD — find via Ghidra's symbol table
   under `*::vftable` matches). Vtable layout per the secondary
   subobject discovered in #54.
2. **Construct a custom `ClassRep<SectorHandoffEvent>` struct** in
   our DLL's static data: 0x80 bytes per the layout in
   `docs/netevent_abi.md`. Most fields can be set from constants;
   `+0x00` points at a custom 9-slot ClassRep vtable we also
   provide.
3. **Override slot 8 of our custom ClassRep vtable** with a real
   `unpack(rep, conn, stream)` function that does our own BitStream
   reads to populate the event's payload. Override slot 7 with a
   `create()` that does `operator_new(size) + *evt = our event
   vftable + base init` (mirrors `FUN_1404E5CE0` from #54).
4. **Register via `NetClassRep::add` (FUN_140418C40)** at attach
   time. `rep->size` = our struct size, `+0x3C` direction = 0
   (bidirectional), classId at `+0x08` = pick an unused id in the
   high range (engine assigns at runtime via the linked-list walk,
   so we may not need to set it ourselves).
5. **Receive handler** runs as part of step 6 of the receive flow.
   Our slot-8 unpack populates the event struct; the engine then
   schedules the event for main-thread processing where our
   `process(rep)` does the SectorHandoff state-machine transition.
6. **Send** with `NetConnection::postNetEvent(FUN_140542CC0)` after
   filling our event struct. The drainer (`FUN_140542630`) calls
   our pack via the symmetric vtable slot — likely slot 7, but
   confirm by walking the send code more carefully (out of scope
   for #56).

## Still open

- Find at least one class whose rep DOES override ClassRep slot 8
  with real unpack code, to confirm the shape of the per-class
  implementation before writing SectorHandoff's real unpack. Candidates:
  any rep whose slot 8 RVA is not `0xA9A50` — easy sweep, can be done
  from the existing `logs/netclassrep_dump.log` data without re-running
  anything.
- vt[10] on the *event* vftable (the classId-write step on the send
  drainer) is still unconfirmed — our pad stub returning 0 causes any
  real client receiving our event to reject with "Invalid packet. (bad
  event class id)". Auto-post via dispatcher bypasses postNetEvent
  (see issue #84 fix), so this only matters if/when we want to engine-
  send our event to a real client.

## Event vftable layout — resolved (chunk 9, issue #87)

Empirical dump of ServerUUIDEvent's vftable at `0x1408A03E8`
(15 slots) cross-referenced with Ghidra decompiles. Confirmed slot map
for the NetEvent secondary subobject vftable as cloned by
`sector_handoff_event.cpp`:

| slot | name | evidence |
|---|---|---|
| 0 | ConsoleObject vtable head (`FUN_140086A30`) | inherited |
| 1 | `_guard_check_icall` | CFG stub |
| 2 | inherited write helper | `FUN_14041E790` |
| 3 | `_guard_check_icall` | CFG stub |
| 4 | inherited write fallback | `FUN_14041DFF0` |
| 5 | `getClassRep()` (`FUN_140086850`) | per-class override of slot 5 makes this our `ourEventGetClassRep` |
| 6 | `_guard_check_icall` | CFG stub |
| **7** | **`pack(NetConnection*, BitStream*)`** | drainer call site (#68) |
| 8–12 | secondary-subobject thunks / inherited | mostly duplicates of 0,2,4 plus CFG stubs |
| **13** | **`process(NetConnection*) -> u8`** | `FUN_1404e5190` — calls `FUN_1404e6130` then `return 1` |
| 14 | indirector / thunk | `FUN_1400a9a40` — `(**p2)(p2, 0)` |

`docs/net_events.md`'s "slot 7 = process" mapping describes the
*primary* `NetEvent` vftable (the ConsoleObject-layout vftable).
The secondary subobject we clone has a different layout, with pack
at 7 and process at 13. Both vftables coexist on the same object
via MSVC multiple-inheritance; the engine's send drainer and
receive path both reach into the secondary subobject vftable.

## Side findings

- Error string `"Event counter is reached its maximum. Dropping connection [%u](%s)."`
  shows there is a per-connection event-id allocator at
  `this + 0x66C` with a wraparound at `-1`. Relevant when
  SectorHandoff timing collides with normal event traffic.
- `"NETDEBUG '%s': Sent %u unordered events with total size of %u"` —
  the binary already has a debug-mode that counts events per send.
  Useful for ratifying our SectorHandoff sends in early testing.
- `"can't submit NetEvent [%d](%s) from non-main thread!"` — fired
  *as a warning*, not an error. The thread-marshal path through
  MainThreadEvent handles it correctly; we don't need to be
  paranoid about which thread `postNetEvent` is called from.
