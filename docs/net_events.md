---
title: NetEvent vtable layout
status: re
domain: reverse-engineering
tags: [netevent, vtable, torque, serialization]
related: [netevent_abi.md, netevent_receive_path.md, sector_handoff_design.md]
updated: 2026-06-26
---

# Network Events — Vtable Layout and Handler Index

The most useful single result of the reverse-engineering pass: every `*Event` class derived from Torque's `NetEvent` exposes the same three hook points at predictable vtable slots. There are **129 NetEvent-derived classes** in the LiF server, each giving you a one-line interception point for that event's `pack` (outgoing), `unpack` (incoming), and `process` (handler).

For the data file itself see `/tmp/lifx_ghidra/event_handlers.tsv` produced by `scripts/ghidra/LifxEventVtables.java` (described in [`reverse_engineering.md`](reverse_engineering.md)).

## The vtable layout

`*Event` classes that inherit from `NetEvent : ConsoleObject` have **13 virtual slots** in MSVC x64 layout. Verified empirically by measuring "how many distinct function pointers appear at each slot across all 129 classes": slots with one shared target are inherited base virtuals; slots with ~127 distinct targets are per-class overrides.

| Slot | Distinct targets across 129 classes | Semantics |
|---|---|---|
| 0 | 127 (per-class) | `~ClassName()` — scalar deleting destructor. |
| 1 | 1 dominant (`0x41E280`) on 129 | `ConsoleObject` inherited virtual (initPersistFields-style). |
| **2** | **128 (per-class override)** | **`pack(NetConnection*, BitStream*)`** — serialize the event for the wire. |
| 3 | `_guard_check_icall` on 129 | `write()` slot — almost never overridden (CFG stub placeholder). |
| 4 | `0x41DFF0` on 129 (inherited) | `NetEvent::write` default impl (calls `pack`). |
| **5** | **128 (per-class override)** | **`unpack(NetConnection*, BitStream*)`** — deserialize from the wire. |
| 6 | `0x4168F0` on 129 (inherited) | base virtual. |
| **7** | **101 (per-class override)** | **`process(NetConnection*)`** — handle the event after unpack. |
| 8 | 93 (often overridden) | `notifyDelivered(NetConnection*, bool madeIt)` — optional. |
| 9 | 74 (sometimes overridden) | extra virtual (varies by class). |
| 10 | `_guard_check_icall` on 129 | unused slot. |
| 11 | shared on ~128 | optional. |
| 12 | shared on 129 | base virtual. |

**Slot 7 (`process`) is the primary hook target for almost every gameplay interception.** It's where the server reacts to what a client just sent (or vice versa). Slot 5 (`unpack`) is where you'd intercept to mutate field values before the engine sees them. Slot 2 (`pack`) is where you'd watch what the server is *about* to send out.

Classes with 14, 15, or 16 virtuals are extensions — usually terrain/tunnel-manager events that add 1–3 derived-class virtuals after slot 12. The slot-7 = `process` mapping still holds; the extra slots are class-specific (and rare).

## Call signatures (for hook stubs)

MSVC x64 always passes `this` in `RCX`, so a Detours hook on any of the three primary slots takes a self pointer plus the documented args:

```cpp
// Slot 2 — pack:
__CM_DECL_INTERNAL(void, __fastcall, _ChatEvent_pack,
                   void* self, void* netConnection, void* bitStream);

// Slot 5 — unpack:
__CM_DECL_INTERNAL(void, __fastcall, _ChatEvent_unpack,
                   void* self, void* netConnection, void* bitStream);

// Slot 7 — process:
__CM_DECL_INTERNAL(void, __fastcall, _ChatEvent_process,
                   void* self, void* netConnection);
```

`NetConnection*` and `BitStream*` are opaque pointers to start with — once you decompile a specific event's `unpack` you'll see exactly which fields it reads from the bitstream and where it stores them in the event object, at which point you can declare a typed struct for `self` and access the message contents directly.

## Caveats

- **CFG stub means "not overridden".** If an event's slot 7 RVA is `0x85F40` (the value of `_guard_check_icall`), that class doesn't define its own `process` — the work happens in a parent class. To hook those, find a more-specific subclass that *does* override, or hook the parent. Examples: `AbilityEvent::process`, `AbilityCancelEvent::process` both delegate; use `AskClientForStartAbilityEvent`, `AbilityCooldownsEvent`, or `AbilityReturnStatusEvent` instead.
- **`*ServerToClient*` is for clients.** Events with `ServerToClientEvent` in their name run their `process` on the client side. Hooking them on the server intercepts the *outgoing* event when it's queued (slot 2 `pack`), not the actual handling.
- **`pack` and `unpack` symmetry isn't enforced.** Most Torque events have a `pack`/`unpack` pair that round-trips bitstream layout; a few don't. If you're mutating, hook `unpack` on the receiving side, not `pack` on the sending side, unless you specifically want to lie about what you sent.

## Selected gameplay events

From `/tmp/lifx_ghidra/event_handlers.tsv`. These are the obvious "first mod" targets. Add to `cm_offsets.h` as needed.

| Event class | pack (slot 2) | unpack (slot 5) | process (slot 7) |
|---|---|---|---|
| `ChatEvent`                              | `0x154CE0` | `0x154D30` | **`0x154D40`** |
| `AchievementEvent`                       | `0x183CF0` | `0x183D60` | **`0x183D70`** |
| `BlueprintsEvent`                        | `0x303920` | `0x303A00` | **`0x303A20`** |
| `BoundedPlayersListEvent`                | `0x35A430` | `0x35A440` | **`0x35A450`** |
| `CharInfoEvent`                          | `0x1C11E0` | `0x1C1280` | **`0x1C12A0`** |
| `CharacterInfoEvent`                     | `0x1C11F0` | `0x1C1290` | **`0x1C1480`** |
| `CharacterNameDataEvent`                 | `0x1B5A10` | `0x1B62E0` | **`0x1B6300`** |
| `CharSelectEvent`                        | `0x52F560` | `0x531930` | **`0x532780`** |
| `ClientInventoryUpdateEvent`             | `0x282810` | `0x282C30` | **`0x282CF0`** |
| `ClientMoveContainerEvent`               | `0x282820` | `0x282C40` | **`0x282D50`** |
| `TrickmoveEvent`                         | `0x36E2B0` | `0x36E830` | **`0x36EA00`** |
| `WeatherEvent`                           | `0x470D20` | `0x471580` | **`0x471850`** |
| `WeaponHitOccured_ServerToClientEvent`   | `0x10DEF0` | `0x10E360` | `0x10E8E0` *(client-side handler)* |
| `BombExplodeDamageRayEvent`              | `0xB82C0`  | `0xB8320`  | **`0xB1040`** |
| `AskClientForStartAbilityEvent`          | `0x3B1AA0` | `0x3B1B80` | **`0x3B1B90`** |
| `AbilityCooldownsEvent`                  | `0x3B1380` | `0x3B13F0` | **`0x3B1400`** |

For the full 130-row table grep `event_handlers.tsv`. To find all events whose name contains a keyword:

```bash
grep -iE "^[^\t]*chat" /tmp/lifx_ghidra/event_handlers.tsv
grep -iE "^[^\t]*Inventory" /tmp/lifx_ghidra/event_handlers.tsv
grep -iE "^[^\t]*Ability" /tmp/lifx_ghidra/event_handlers.tsv
```

## Adding an event hook to LiFx

Worked example for `ChatEvent::process`:

**1. `source/server/cm_offsets.h`:**

```cpp
enum CmOffset : U32 {
    // …existing entries…
    CHAT_EVENT_PROCESS = 0x154D40,
};
```

**2. Declare + instantiate the original:**

```cpp
// hooks/gameplay/hook_chat.h
__CM_DECL_INTERNAL(void, __fastcall, _ChatEvent_process,
                   void* self, void* netConnection);

namespace Hooks::Gameplay {
    void ChatProcess(void* self, void* netConnection);
}

// hooks/gameplay/hook_chat.cpp
__CM_INSTATNTIATE(_ChatEvent_process);

void Hooks::Gameplay::ChatProcess(void* self, void* netConnection) {
    // For now: just log that it fired.
    Con::Echo("[chat] event received  self=%p  conn=%p", self, netConnection);

    // Then defer to the real implementation:
    _ChatEvent_process(self, netConnection);
}
```

**3. Attach in `Lifx::Server::AttachHooks()` (the currently-empty stub):**

```cpp
void Lifx::Server::AttachHooks() {
    __CM_ATTACH_HOOK(CmOffset::CHAT_EVENT_PROCESS,
                     _ChatEvent_process,
                     Hooks::Gameplay::ChatProcess);
}

void Lifx::Server::DetachHooks() {
    __CM_DETACH_HOOK(_ChatEvent_process, Hooks::Gameplay::ChatProcess);
}
```

Once the hook is in place, the next iteration is to figure out the struct layout of `ChatEvent` (decompile its `unpack` in Ghidra — you'll see `bstream->readString(self+0xXX); bstream->readInt(self+0xYY)` etc.) and replace `void* self` with a typed `ChatEvent*` whose fields you can read.
