---
title: NetEvent ABI
status: re
domain: reverse-engineering
tags: [netevent, classrep, torque, serialization, sector-handoff]
related: [net_events.md, netevent_receive_path.md, sector_handoff_design.md, dispatcher_wire_format.md]
sources: [source/server/cm_offsets.h, source/server/hooks/netevent/hook_netclassrep_dumper.cpp, source/server/hooks/netevent/hook_netclassrep_dumper.h, scripts/ghidra/LifxNetEventABI.java, docs/netevent_receive_path.md, docs/sector_handoff_design.md]
updated: 2026-06-26
---

# NetEvent ABI

**TL;DR.** RE of the Torque-derived NetEvent registration/serialization ABI in
`ddctd_cm_yo_server.exe` (image base `0x140000000`), originally issue #52 and
closed out by the runtime dumper in #54 / PR #55. **Settled negative result:**
there is **no per-class `pack`/`unpack`** anywhere a static binary exposes them
— not on the event vtable, not on the `ConcreteClassRep<T>` vtable, not in the
ClassRep data struct. The runtime dump of **247** ClassReps confirms the
ClassRep struct is pure metadata (zero `EXEC`-tagged qwords). In this Torque
variant serialization is **runtime-built** by a generic per-rep marshaler the
EventManager constructs from `(rep.size, rep.flags, rep.namespace)`. Practical
consequence for SectorHandoff (#45 / #50): don't try to mirror an existing
class's `pack`/`unpack` — implement our own with direct `BitStream` calls in our
factory and register via `NetClassRep::add`. The **receive** side is a separate
RE problem; its first pass lives in `docs/netevent_receive_path.md` (#56).

Two of the RVAs below are named constants in
`source/server/cm_offsets.h` — cross-checked and consistent:

| Symbol in `cm_offsets.h` | Value | This doc's name |
|--------------------------|-------|-----------------|
| `NETCLASSREP_ADD`        | `0x418C40` | `NetClassRep::add` / `FUN_140418C40` |
| `SERVERUUIDEVENT_SEND`   | `0x4E7370` | `ServerUUIDEvent::send` factory / `FUN_1404E7370` (post call site, dumper hook) |

Every other RVA in this page is a Ghidra `FUN_*` / data label only — **not**
present in `cm_offsets.h`. Don't assume a named constant for them.

Scripts: `scripts/ghidra/LifxNetEventABI.java`, `LifxNetEventVtable.java`,
`LifxNetEventABI2.java`, `LifxNetEventABI3.java`. (Receive-side scripts
`LifxNetEventPostVfn.java` / `LifxNetEventCppRefs.java` are documented in
`docs/netevent_receive_path.md`.)

> Original framing: targets were the three calling conventions called out by §7
> of `docs/sector_handoff_design.md`. The first pass was **static RE only**; one
> of the three (pack/unpack fn pointers on the ClassRep) is only initialized at
> runtime, so the full answer waited on a DLL-side runtime dumper — now landed.

## What is concretely settled

### 1. `NetClassRep::add` — `FUN_140418C40` (`NETCLASSREP_ADD = 0x418C40`)

```c
void FUN_140418c40(longlong rep) {
    *(longlong *)(rep + 0x50) = DAT_140bc00b0; // next = old head
    DAT_140bc00b0 = rep;                       // head = us
}
```

Plain singly-linked-list head insert. Each `ConcreteClassRep<T>` stores its
next-pointer at **offset +0x50**. The global list head is at
`0x140BC00B0`. The engine assigns network class IDs by walking this list
at world startup. (`cm_offsets.h` documents the same head `DAT_140BC00B0` and
next-ptr `+0x50`, and notes the hook is gated behind
`<dumpNetClassRep>1</dumpNetClassRep>`.)

**Implication for SectorHandoff:** the DLL's static initializer (run from
`DllMain` / our existing attach path) just needs to allocate a ~0x80-byte
ClassRep, point its `+0x00` at our own ClassRep vtable (the struct is pure
metadata — pack/unpack/create live as vtable slots, not raw struct
pointers; layout below), and call `FUN_140418C40` on it. It will land in
the same registry as `ServerUUIDEvent`.

### 2. The "post" call site — *not* a NetConnection vtable slot

Re-reading the factory call shows the access is to a **member variable**,
not a vtable slot:

```c
(*(code *)**(undefined8 **)(param_1 + 0x1f8))(
    (undefined8 *)(param_1 + 0x1f8), evt, param_3, param_4, ...);
```

Parsed: `NetConnection->[+0x1F8]` is a pointer to some aggregated sink
object; `**` dereferences to its vftable; slot 0 of that sink's vtable
is invoked with `(sink, evt, …)`. So NetConnection holds an
**EventQueue / EventSink** sub-object pointer at offset +0x1F8, and we
post by calling that sink's first virtual method.

This rules out our earlier assumption that the post vfn was at
NetConnection vtable +0x1F8. Searched all three NetConnection vtable
symbol candidates (`0x1408BE630`, `0x1408BE908`, `0x1408BE940`) — only
`0x1408BE630` is a real vtable, and its slot 63 is just
`_guard_check_icall`. The other two addresses are RTTI metadata
adjacent to the vtable, not vtables themselves.

> **RESOLVED in a later pass (see `docs/netevent_receive_path.md`, #56).** The
> sink at `NetConnection+0x1F8` was captured live by the PR #55 dumper as
> `sink @ 0x140783118` = the **GameConnection secondary-base subobject vtable**.
> Slot 0 of that vtable is `NetConnection::postNetEvent` = `FUN_140542CC0`
> (string `"NetConnection::postNetEvent"` embedded in its error path). It is the
> **enqueue only** — it refcounts the event and pushes a 16-byte
> `{event_ptr, eventId, …}` record onto a per-connection queue at `this+0x5E8`
> (unordered) or `this+0x608` (ordered), selected by `event->refcount_flag` at
> `+0x14`; the actual serialization runs in the queue drainer `FUN_140542630`.
> The "Open work" below is kept for history.

**Open work (now closed, kept for history):** identify the sink class via DLL
breakpoint on `FUN_1404E7370`, log `*(NetConnection*+0x1F8)`, dump its
vtable. Likely candidates from naming conventions: `NetEventQueue`,
`EventBuffer`, `Connection::EventBuffer`. — Actual answer: GameConnection
subobject vtable `0x140783118`, slot 0 = `FUN_140542CC0`.

### 3. ServerUUIDEvent vtable layout — *not* where pack/unpack live

`ServerUUIDEvent::vftable` resolves to `0x1408A03E8`. Walking it gives
**only 7 slots**, all generic Console/EngineTypeInfo accessors:

| Slot | RVA       | Body                                              | Likely role            |
|-----:|-----------|---------------------------------------------------|------------------------|
| 0    | `0x86A30` | `_unused(); if (flags & 1) free(this); return;`   | `~ServerUUIDEvent` (scalar deleting dtor) |
| 1    | `0x85F40` | `return;` (CFG-thunk stub)                        | placeholder            |
| 2    | `0x41E790`| `return &DAT_140bc3860;`                          | `getTypeInfo()` — returns the global **EngineTypeInfo** rep (this was misidentified as ServerUUIDEvent's ClassRep in an earlier pass — it's not, it's shared) |
| 3    | `0x85F40` | stub                                              | placeholder            |
| 4    | `0x41DFF0`| `snprintf("class: %s", typeName(this), …)`        | `describeSelf()`       |
| 5    | `0x86850` | `return 0;`                                       | default accessor       |
| 6    | `0x85F40` | stub                                              | placeholder            |

**Conclusion:** pack/unpack are **not direct C++ virtuals** on
ServerUUIDEvent in this Torque variant. They are stored as **function
pointers on the per-class ConcreteClassRep struct** — *or so the static pass
hypothesized*; §"Runtime resolution" and §"Dead end" below disprove even that.

**Open work (closed by #54):** hook `FUN_140418C40` (NetClassRep::add) from the
DLL — every call gives us a rep pointer; walking it after init exposes
the pack/unpack offsets. We can compare across registered classes (the
script-binding registry already enumerates ~50 classes per #49) and find
the consistent slot pair. — Done; no such slot pair exists (see below).

## What also got mapped in passing

- **TS console-cmd registrar** = `FUN_140404B60`. Args (in TorqueScript
  binding parlance):
  `(rep, flags, name, fnptr, namespace, minArgs, maxArgs, callable_flag, vtable_ptr)`.
  This is how `SendServerUUIDEvent(NetConnection*)` reaches script —
  invoked from registry slot `0x6F6F0`.
- **`EngineFunctionInfo::add`** = `FUN_14041DF20` (already seen in #49
  via the script-binding slots). Distinct from `NetClassRep::add` but
  uses the same fields-then-list-link pattern (head at `0x140BC2140`,
  next-ptr at +0x80 on the rep).
- **`ConcreteClassRep<NetConnection>`** static instance @ `0x1408BCBF8`.
- **`ConcreteClassRep<GameConnection>`** static instance @ `0x14077ECB8`.
  GameConnection is the concrete type the server uses; the LiFx hook
  layer should observe transitions through it for player-cap and
  sector-arrive callbacks.
- **Server-side script binding** for ServerUUIDEvent dispatches to
  `FUN_1404E7260` which `__RTDynamicCast`s its arg to
  `NetConnection::RTTI_Type_Descriptor` — confirmed in the symbol table.

## Runtime resolution (issue #54 — capture from one boot)

The dumper in `source/server/hooks/netevent/hook_netclassrep_dumper.cpp`
captured **247** reps from one server boot with one connected player.
Result: the ClassRep struct layout is fully mapped, but pack/unpack
turned out **not** to be on the ClassRep vtable either — they aren't
in any of the static vtables this binary exposes. See "Dead end" below.

### ClassRep instance struct (0x80 bytes)

Every entry on the global classRep list has this layout. Zero qwords
were `EXEC`-tagged across all 247 reps — confirming this struct is
pure metadata, not a function-pointer table.

| Offset | Type | Field | Notes |
|-------:|------|-------|-------|
| `+0x00` | ptr  | **`ConcreteClassRep<T>::vftable`** | Different per class. See "ClassRep vtable" below. |
| `+0x08` | u64  | classIdx                  | Distinct per class, e.g. 0x6B for ServerUUIDEvent. |
| `+0x10` | u64  | const `0x08`              | Group / category flag. |
| `+0x18` | ptr  | (heap) name string ptr    | Allocated at runtime, not visible statically. |
| `+0x20` | u64  | reserved                  | Always 0. |
| `+0x28` | ptr  | sibling rep ptr           | Often == `+0x50`, sometimes a different sibling. |
| `+0x30` | ptr  | per-class metadata block  | All zeros statically; runtime-populated. |
| `+0x38` | u64  | flags                     | 0xFFFFFFFF or 0x100000000 — class-category bit pair. |
| `+0x40` | u32  | **instance size**         | e.g. `0x48` for ServerUUIDEvent. |
| `+0x48` | ptr  | (heap) ptr                | Runtime-allocated metadata. |
| `+0x50` | ptr  | **next-ptr on global list** | Walked by `NetClassRep::add`. |
| `+0x58` | ptr  | sibling / parent rep ptr  | Optional. |
| `+0x60` | ptr  | (heap) ptr                | Likely TS-binding state. |
| `+0x68` | u64  | const `1`                 | Init flag. |
| `+0x70` | ptr  | **`&namespace_root`**     | Always `0x140735B40` — the global namespace root. |
| `+0x78` | u64  | reserved                  | Always 0. |

### ClassRep vtable — 9 slots, **no pack/unpack**

Walked the vtable at `0x1408A1C08` (ServerUUIDEvent's rep) against
three control reps (sizes 0x138 / 0x108 / 0x68). Slots that vary per
class isolate the truly-per-class overrides. Result:

| Slot | RVA (ServerUUIDEvent) | Role |
|----:|------|------|
| 0   | `0x4E5D50` | **`setData`** — TS arg parser; `__RTDynamicCast` from ConsoleObject* to the concrete event type. |
| 1   | `0xA9B60` | `getCategoryName` — returns global namespace ptr. |
| 2-6 | (consistent across all reps) | Inherited ConsoleObject base methods (`getDoc`, `getCategoryDoc`, etc.). |
| 7   | `0x4E5CE0` | **`create()`** — `operator_new(0x48)` + `*evt = ServerUUIDEvent::vftable`. This is the factory we already knew as FUN_1404E60D0, just one wrapper deeper. |
| 8   | `0xA9A50` | `destroy` — `_guard_check_icall × 2` + `FUN_140418710` (generic teardown). |
| ≥9  | inline ASCII name | "TypeServerUUIDEvent" + padding. Vtable terminates here at offset `0x48`. |

### Event vtable — also no pack/unpack

PR #53's `ServerUUIDEvent::vftable` walk at `0x1408A03E8` stopped at
slot 7. That's correct — the symbol covers exactly 7 slots (the
ConsoleObject-derived virtuals) and slot 7's qword (`0x1409C5350`) is
RTTI metadata. The decompiles at slots 8+ that we re-walked turned out
to belong to the **adjacent** `TerrainAttacheEvent` vtable (its ASCII
name string is visible at offsets `+0x78..+0x88`). Not part of
ServerUUIDEvent's hierarchy.

### Sink (`NetConnection + 0x1F8`) — not captured this run

The dumper hook fired during boot with `conn=0x0` because the engine
calls `SendServerUUIDEvent` once at startup with a NULL peer (no
dispatcher attached in YO). The connected player's `GameConnection*`
was alive but `SendServerUUIDEvent(LocalClientConnection)` wasn't
invoked from the in-game console, so the sink dump didn't capture a
real connection.

To capture: re-enable `<dumpNetClassRep>1</dumpNetClassRep>`, connect
a player, and from the in-game console run:

    SendServerUUIDEvent(LocalClientConnection);

The first invocation with a non-NULL connection will append the sink
address + its vtable to `logs/netclassrep_dump.log`.

> **Done (PR #55 follow-on):** that capture yielded `sink @ 0x140783118`
> (GameConnection secondary-base subobject vtable), slot 0 =
> `NetConnection::postNetEvent` `FUN_140542CC0`. Full receive/send call chain
> in `docs/netevent_receive_path.md`.

## Dead end: pack/unpack are not in any static vtable

After ruling out:
- ServerUUIDEvent's event vtable (`0x1408A03E8` — 7 inherited slots only),
- ServerUUIDEvent's ClassRep vtable (`0x1408A1C08` — 9 slots: setData / getCategoryName / inherited / create / destroy),
- ClassRep `+0x30` (zero-initialized in .data),

there is no static location pack/unpack could live. The most likely
explanation in this Torque variant: NetEvent serialization is handled
by a **generic per-rep marshaler** built at runtime by the EventManager
from `(rep.size, rep.flags, rep.namespace)`. Per-class pack/unpack
overrides — if they exist at all — are installed dynamically by code
we'd have to capture through a debugger or by hooking the receive path.

This is a **settled negative result, not a dead end to keep poking at
statically.** Mid-investigation we kept hypothesising "the next layer must
hold pack/unpack." It doesn't. Stop walking static vtables for it.

### Practical implication for SectorHandoff (#45, #50)

Don't try to mirror an existing class's pack/unpack. Instead:

- Implement SectorHandoff's pack/unpack with **direct BitStream calls**
  in our own factory function (we already know `operator_new(size)`,
  the `_vftable` write, and the post path via `NetConnection+0x1F8`
  → `FUN_140542CC0`).
- Register via `NetClassRep::add` (we own `FUN_140418C40` /
  `NETCLASSREP_ADD = 0x418C40` from PR #53) with a ClassRep we statically
  construct in our DLL: vtable pointing at our own 9-slot ClassRep vtable
  that implements the inherited accessors trivially.
- The **receive** side is the remaining unknown. We need a separate RE
  pass that finds where the engine reads incoming events off the wire
  and dispatches them to per-class handlers. **That pass has started** in
  `docs/netevent_receive_path.md` (#56) — send/enqueue (`FUN_140542CC0`)
  and the drainer (`FUN_140542630`) are mapped; the inbound dispatch to a
  per-class unpack is still being traced there, not here.

## What stays open for the next pass

| Gap | Why static RE can't close it | Resolves via |
|-----|------------------------------|--------------|
| Pack/unpack fn-pointer offsets on `ConcreteClassRep<T>` | They aren't on the rep vtable or in the rep struct at all (247-rep dump, zero `EXEC` qwords). Serialization is runtime-built by a generic marshaler. | Not an offset hunt anymore — RE the EventManager's runtime marshaler / hook the live receive path. |
| Identity of the sink class at `NetConnection+0x1F8` | Field is allocated dynamically in the NetConnection ctor; the static binary only shows the field-store site. | **CLOSED:** `sink @ 0x140783118` (GameConnection subobject vtable), slot 0 = `FUN_140542CC0`. See `netevent_receive_path.md`. |
| Exact wire byte order (BitStream helper API) | Lives behind the runtime marshaler, not behind a static fn pointer. | Trace the drainer `FUN_140542630` and the inbound unpack path (`netevent_receive_path.md`). |

## Status & provenance

**Runtime-verified (issue #54 / PR #55 dumper, one boot, one player):**

- `NetClassRep::add` = `FUN_140418C40` = `NETCLASSREP_ADD 0x418C40`, head
  `DAT_140BC00B0`, next-ptr at rep `+0x50`.
- **247** ClassReps captured; ClassRep struct is **0x80 bytes of pure
  metadata**, zero `EXEC`-tagged qwords; field layout in the table above.
- ClassRep vtable `0x1408A1C08` has **9 slots** with **no pack/unpack**
  (`setData 0x4E5D50`, `getCategoryName 0xA9B60`, inherited, `create 0x4E5CE0`,
  `destroy 0xA9A50`).
- ServerUUIDEvent event vtable `0x1408A03E8` has **7 slots**, all generic
  ConsoleObject virtuals — **no pack/unpack**.
- **Negative result is confirmed:** per-class `pack`/`unpack` do not exist in
  any static vtable; NetEvent serialization is runtime-built. A sector-handoff
  receive path therefore needs separate RE.
- Sink at `NetConnection+0x1F8` = `0x140783118`, slot 0 = `FUN_140542CC0`
  (`NetConnection::postNetEvent`) — captured in the PR #55 follow-on and fully
  documented in `docs/netevent_receive_path.md`.

**Static RE only (issues #52 / #53 — Ghidra decompiles, not runtime-checked):**

- `SERVERUUIDEVENT_SEND 0x4E7370` post call-site decode (`(*(*conn[+0x1F8])[0])`).
- The per-slot RVA bodies in the §3 event-vtable table (dtor `0x86A30`,
  stubs `0x85F40`, `getTypeInfo 0x41E790` → `DAT_140BC3860`, `describeSelf
  0x41DFF0`, accessor `0x86850`).
- The "mapped in passing" registrars: TS console registrar `FUN_140404B60`
  (slot `0x6F6F0`), `EngineFunctionInfo::add FUN_14041DF20` (head `0x140BC2140`,
  next `+0x80`), `ConcreteClassRep<NetConnection> 0x1408BCBF8`,
  `ConcreteClassRep<GameConnection> 0x14077ECB8`, binding `FUN_1404E7260`.
- Global namespace root `0x140735B40`.

**Cross-checked against `source/server/cm_offsets.h`:** only `NETCLASSREP_ADD`
(`0x418C40`) and `SERVERUUIDEVENT_SEND` (`0x4E7370`) are named constants there,
both consistent with this page. All other RVAs are Ghidra labels with no
`cm_offsets.h` entry; do not assume a named constant for them.

## Trade-off note

We could have kept digging statically — walking every CRT static-init slot
to find ServerUUIDEvent's ClassRep init, then back into its writers.
But the binary's `ConcreteClassRep<T>` instances are template-generated,
unnamed in the static stripped binary, and unsymbolized. A 30-line
runtime dumper at attach time gave the same answer in seconds (and gave the
negative result that ended the offset hunt). Not worth burning another
static RE pass on it.
