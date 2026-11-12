---
title: Client inventory lifetime crash on stat changes
author: Zbigi
assistance: ChatGPT (Codex)
status: re
domain: client
tags: [inventory, lifetime, willpower, crash, callbacks, abilities]
related: [custom_tree_drops.md, principles.md, loader_and_injection.md]
sources: [docs/evidence/tree_felling_reference.json, source/client/client_runtime.cpp, source/client/client_offsets.h]
updated: 2026-11-12
---

# Client inventory lifetime crash on stat changes

The reference client registers a raw root-inventory-container callback for
character stat changes, but its destructors leave that subscription behind.
A later Willpower change can invoke the retired container and reach an invalid
inventory GUI call. Felling a tree can expose the problem through a stat update;
changing the tree's drop ID or disabling skill gain does not repair the callback
lifetime. **The current Plus client does not implement this repair.**

Prepared with help from ChatGPT (Codex), in collaboration with Zbigi, from
documents and analyses collected over four months of work on LiFXXL, a
community project based on a modified LiF:YO server.

## Evidence and the exact limitation

The 2026-10-17 incident dump records an execute access violation `0xC0000005`
at `RIP=0x1`, with exception parameters `[8, 1]`. The top return address is
client RVA `0x89BFA2`, immediately after the indirect call at `0x89BF9C`.
These exception fields and the top stack entry were independently re-read on
2026-11-12. The preceding instruction loads a vtable from `RCX`; the call uses
slot `+0x680` inside `GuiInventoryContainer::init`.

The historical stack reconstruction proceeds through capacity refresh
(`0x42DCDA` return), stat-signal emission (`0x3372F3`) and receipt of character
stats (`0x337CF8`). It is a combat-session dump, not a dump of every reported
tree failure. It does not preserve the heap history needed to prove exactly
when that incident's object was freed. The lifetime defect and controlled
reproduction support the mechanism; they do not identify every action crash
as the same fault. Match a new report to its own exception, stack and build.

All client RVAs here refer to original Windows x64 `yo_cm_client.exe` 1.4.4.5:
`24,607,688` bytes, SHA-256
`1D3F7FD3E64B119DC032CDA77D353AFEFC340AF46C6B7AE30890A9BC99E37999`.
The [evidence](evidence/tree_felling_reference.json) records native instruction
anchors and the separate identity of the repaired client used for regression.
These are client addresses, not entries for server `CmOffset`.

## Lifetime sequence

1. Constructor `0x42D920` registers callback `0x42DE70` on the stat signal at
   `CmCharacterInfo+0x1C8`. The receiver is the raw container pointer; the
   helper at `0x14D5D0` does not create a weak or owning subscription.
2. Destructors `0x42DB40` and `0x42DBD0` release fields and enter the base
   destructor, but do not remove this subscription. The second path also
   implements scalar deletion when its flag requests it.
3. The client's stat setter `0x3371E0` emits the signal. Callback `0x42DE70`
   filters `CmStatType=2` (Willpower) and passes its new value to `0x42DC70`.
4. That function updates container capacity fields `+0x110/+0x114`, reads
   the GUI reference at `+0xF0` and passes the container ID from `+0x60` into
   `GuiInventoryContainer::init`. With a retired receiver these accesses
   already violate the object's lifetime, even if the memory remains readable.

The signal node is `0x28` bytes: next/previous `+0/+8`, priority `+0x10`,
receiver `+0x18`, method `+0x20`. The signal's mutex is at `+0x38`
(`CmCharacterInfo+0x200`); its active-iterator count and pointer are at
`+0x18/+0x20`. The character-info singleton is at client RVA `0x15E0A48`.

## Repair contract for an injected client hook

Disconnect the exact **receiver + callback method** pair before either root
destructor changes its vtable or releases fields. Keep all unrelated receivers
and other methods on the same receiver subscribed. A valid removal must:

- use the signal's own lock;
- find the node for this container and callback `0x42DE70`;
- advance every active emission iterator that points to the node, including
  nested emissions, before unlinking it;
- unlink with the native node operation (`0x634990`) and release the node with
  the compatible native allocator (`0xDB95C0`, size `0x28`);
- release the lock and preserve normal destructor/shared-pointer cleanup;
- handle shutdown after the character-info singleton has gone, matching the
  native signal-removal pattern.

The engine routine at `0x3741F0` is a useful removal **pattern**, not a generic
function that can be called unchanged for this container. It hardcodes a
different method (`0x379D80`) at `0x374255`; reusing it blindly would not remove
the root callback. Adapt the receiver/method matching and preserve the iterator
and allocator behavior.

The existing LiFXXL repair enters before the seven-byte vtable loads at
`0x42DB4A` and `0x42DBD6`, preserving `RCX=this`, the deleting-destructor flag
in `EDX` and the original vtable value in `RAX`. Those are verified reference
seams, not instructions to edit an installed EXE. For Plus, follow
[the DLL-injection rule](principles.md#rule-1--never-byte-patch-the-game-binaries):
implement the lifetime operation in its client DLL, verify both destructor
paths and ABI, and install before containers are created. A server-only hook
cannot disconnect an object inside a player's client.

Locking Willpower, blocking skill progression, closing the inventory window,
or skipping an invalid-looking pointer does not remove the stale subscription.
A running client can also retain old callbacks after a code change; acceptance
must start with a fresh client process.

## Why abilities and stat direction matter

The inspected current skill XML contains `57` skills; `22` name Willpower as
a primary or secondary stat. That list alone is insufficient coverage:

| Ability | Skill in the inspected data | Primary / secondary |
| --- | --- | --- |
| `7`, Lower Ground Level | `1`, Artisan | Int / Str |
| `34/36`, Cut Down | `1`, Artisan | Int / Str |
| `35`, Uproot | `1`, Artisan | Int / Str |
| `37/38/39`, Saw out results | `8`, Carpentry | Con / Str |

At the relevant stat cap, redistribution can lower Willpower even for a skill
whose own stats are Int/Str. Verify the actual attribute delta. Skill amount,
skill direction and the five attribute directions are separate state.
The client stat-event loop passes the attribute type and value to the setter;
the container callback does not receive an ability ID. Lower Ground Level is
therefore an acceptance candidate through the same stat-update mechanism,
not a separately proven terrain-triggered crash in the incident dump.

## Regression and gameplay acceptance

On 2026-11-12 the existing LiFXXL repair was rechecked against the materialized
client image using Unicorn `2.1.4`: **22 scenarios passed**, including:

- both original destructor paths as negative controls, reproducing `RIP=1`
  with return `0x89BFA2` after controlled reuse of retired memory;
- both repaired destructor paths and deletion flags; detachment before member
  release; preservation of other subscriptions;
- live capacity updates on Willpower increase/decrease, including open/closed
  GUI fixtures and unchanged values;
- removal during signal emission and active-iterator repair;
- `128` create/destroy cycles reusing an address, without accumulated callbacks;
- shutdown after the singleton is absent, stack alignment and nonvolatile
  register preservation.

These execute native constructor/destructor/setter/signal/capacity instructions.
Allocator, mutex, base-container lifecycle, initial stat getter, final GUI
refresh and retired-memory reuse are fixtures. This is not a game session,
full GUI rendering or proof that every skill generates the intended stat delta.
The harness and test-image fingerprints are recorded in the evidence; this
contribution does not publish that separate executable or its patch machinery.

Gameplay acceptance must record the loaded client identity and Willpower
before/after, first after fresh login and then after inventory recreation.
Exercise increase and decrease, open and closed inventory GUI, stock and custom
felling, and the relevant non-felling actions. Confirm capacity, inventory
contents and continued client operation. An unchanged attribute or merely
reaching the server-ready state does not cover the fault.

## Status & provenance

The existing documents describe historical deployment as well as native tests;
neither proves the identity of every player's current executable. This review
checked the original client code, historical dump fields, current skill XML and
the existing repair's 22 scenarios. No game/server was launched, no executable
was modified, and no new gameplay trial was performed.

The result is an evidence-backed client repair contract and a cross-reference
from tree integration. The repair remains to be implemented in Plus. Other
crashes during felling need their own diagnosis.
