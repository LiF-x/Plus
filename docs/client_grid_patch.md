---
title: Client 9x9 grid-size patch
status: re
domain: client
tags: [client, exe-patch, world-grid, federation, handoff]
related: [loader_and_injection.md, sector_handoff_design.md, conventions.md]
sources: [source/loader/pdh_loader.cpp, docs/loader_and_injection.md, docs/conventions.md, source/server/cm_offsets.h]
updated: 2026-06-26
---

# Client 9x9 grid-size patch

> **Not sanctioned.** This documents a raw byte-patch of the client exe. **The project rule is "never patch the client/server exe binaries."** All client behaviour changes go through DLL injection (the same `pdh.dll`-proxy + Detours pattern LiFx uses on the server — see [`loader_and_injection.md`](loader_and_injection.md)), never a binary edit. This page records the clamp's location as a verified RE finding so we know **where to hook it from an injected DLL**; we do **not** apply the byte-patch in production.

## TL;DR

A single-byte change at file offset `0x189554` in `yo_cm_client.exe`, `0x03` → `0x09`, makes the client accept a 9x9 world grid instead of the default 3x3 — it stops the client rejecting coordinates beyond ±3 cells. Community-found (Pabluuz, Feb 2025), community-tested. We care about it for **federation**, not single-server map expansion: it's the exact clamp that would otherwise reject the combined coordinate envelope during the dual-shard render moment of a seamless cross-server handoff. The byte-patch is the *evidence*, not the *implementation* — lift the same clamp via a client-side LiFx DLL hook.

## The finding

| Property | Value |
|---|---|
| File | `yo_cm_client.exe` (the LiF client; Steam may launch it under a different name — see *Naming*) |
| Location | file offset `0x189554` (raw byte index into the on-disk image — **a file offset, not an RVA**) |
| Patch | byte `0x03` → `0x09` |
| Effect | client accepts a 9x9 world grid instead of the default 3x3; stops rejecting coords beyond ±3 cells |
| Provenance | community find by Pabluuz, Feb 2025, for extending LiF:YO maps beyond the default 3x3; community-tested |

The community recipe (PowerShell), reproduced for the record — **do not run this; it's the prohibited path**:

```powershell
$offset = 0x189554
[byte[]]$bytes = Get-Content yo_cm_client.exe -Encoding Byte -Raw
$bytes[$offset] = 0x09
,$bytes | Set-Content yo_cm_client.exe -Encoding Byte
```

`0x03` is the per-axis grid dimension (the *N* in an *N*×*N* world grid) that the client uses to validate/clamp world coordinates: the default `0x03` yields a 3x3 grid; bumped to `0x09` it opens the envelope to a 9x9 grid and the client no longer clamps or rejects coordinates that fall outside the default 3x3 footprint.

### Offset is a file offset, not a server RVA

Throughout these docs an **RVA** is an address relative to the *server* image base `0x140000000` (`ddctd_cm_yo_server.exe`), and every constant in [`cm_offsets.h`](../source/server/cm_offsets.h) is a server RVA. `0x189554` here is none of those: it's a **byte index into the client file** `yo_cm_client.exe`, a different binary with its own (unrecorded) image base and section layout. Converting it to a client RVA would require the client PE's section headers, which we have not captured. Do not treat `0x189554` as comparable to the server RVAs elsewhere in the knowledge base.

### Cross-check against `cm_offsets.h`

Grepped `source/server/cm_offsets.h` for `0x189554` / grid / clamp constants: **no match**, as expected — `cm_offsets.h` catalogues *server* offsets only, and this is a client-file offset. There is therefore no named `CmOffset` constant for it and no conflict to flag. (For context, the nearest client reference in that file is a comment naming the client's `CLIENT_OPEN_FILE_STREAM` at `0x61E620` — unrelated to this clamp, just confirming the file occasionally annotates client-side addresses.)

## Why this matters: federation, not map expansion

We do **not** want a 9x9 single-server map. The clamp is interesting only for **seamless cross-server handoff** (the federation work; see [`sector_handoff_design.md`](sector_handoff_design.md)).

Each federated shard still runs as a normal 3x3 world. The clamp becomes load-bearing for exactly one instant — the **hot primary swap** during a seamless handoff (chunk 18+):

- During the swap the client briefly renders ghosts from **both** the source shard A **and** the destination shard B at the same time.
- B's ghosts are offset by **~6 km** from A's in the client's single world frame.
- That combined view places coordinates **outside the 3x3 envelope**, and the default client clamps or rejects them — which is precisely what the `0x03` value enforces.

So lifting the clamp (to `0x09` worth of headroom, or equivalent) is what lets the client hold both shards' entities on screen during the swap without coordinate rejection.

For the earlier federation milestones (chunks 15c–17) this is **not** on the critical path. Pull it in only at the **hot-swap milestone**.

## The sanctioned implementation: hook it, don't patch it

The byte-patch proves two things:
1. The coordinate clamp lives at client file offset `0x189554`.
2. `yo_cm_client.exe` is binary-patchable in production — i.e. some federation client problems may turn out to be small, local code sites rather than requiring deep refactors.

But per project rule, **we never ship the byte edit**. Instead:

- Design the clamp lift as a **client-side LiFx DLL hook** — the same `pdh.dll`-style proxy + Detours injection pattern documented in [`loader_and_injection.md`](loader_and_injection.md), applied to the client process. Hook the validation code that reads the `0x03` half-extent and substitute the wider bound at runtime.
- A loose `.cs` script dropped into `scripts/client/` is allowed (the engine already searches for loose source; it isn't an exe edit) — but Steam's file-verify can clobber it, so it's only suitable for low-cost TS-layer additions, not for this clamp.
- Editing `.cs.dso` (compiled script) is **also** prohibited: a `.dso` is a shipped binary file, so editing it is binary patching under the same rule. To ship script changes durably, have the injected DLL run a `Con::Evaluate` at startup rather than editing the `.dso`.

## Naming

The memory and the community recipe call the client `yo_cm_client.exe` (its internal/build name). Project [`conventions.md`](conventions.md) / the docs index refer to "the client" as `LiF.exe` "(or whatever the LiF Steam install names it)". These are the same binary under different launcher names; the offset applies to the actual client exe regardless of the on-disk filename.

## Status & provenance

- **Verified (externally):** the byte-patch `0x03` → `0x09` at file offset `0x189554` enabling 9x9 worlds is a **community find (Pabluuz, Feb 2025) and community-tested** on `yo_cm_client.exe`. We have **not** re-verified it in this project's environment, and per the no-exe-patching rule we have **not** applied it.
- **Inferred / RE interpretation:** that `0x03` is the per-axis grid dimension used for client coordinate clamp/validation, and the semantics of the federation hot-swap (~6 km B-ghost offset, combined-view coords leaving the 3x3 envelope, chunk-18 timing) come from the federation design notes — design-stage reasoning, **not** runtime-verified here.
- **Confirmed in-repo:** `cm_offsets.h` contains no constant for `0x189554` (it is a client file offset, outside that file's server-RVA scope), so there is no named constant and no conflict.
- **Policy:** byte-patching the exe is off the table; the canonical path is the injected client DLL hook described above and in [`loader_and_injection.md`](loader_and_injection.md).
