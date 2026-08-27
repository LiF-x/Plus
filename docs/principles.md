---
title: Project principles & hard rules
status: reference
domain: conventions
tags: [no-exe-patch, dll-injection, public-docs, policy, detours]
related: [loader_and_injection.md, client_grid_patch.md, conventions.md, contributing.md, architecture.md]
sources: [source/server/cm_offsets.h, source/loader/pdh_loader.cpp, docs/loader_and_injection.md, docs/client_grid_patch.md, docs/conventions.md, docs/architecture.md]
updated: 2026-06-26
---

# Project principles & hard rules

Two project-wide rules override every other consideration in LiFx and are not up for case-by-case
negotiation. **(1) Never byte-patch the client or server binaries** — all behavioural changes go
through DLL injection (the `pdh.dll`-proxy + Detours pattern) or loose `.cs` source, never an edit to
a shipped binary file. **(2) The public docs describe only the TorqueScript-exposed API** — every
engine RVA, vtable slot, struct layout, build step, and the mod-DLL filename stays in this private
knowledge base, never in the externally-mirrored `ghdocs/docs/` set. Both are stated directly by the
project lead; this page records them as enforceable conventions with the reasoning, so a contributor
(human or agent) can apply the test without re-deriving it.

---

## Rule 1 — Never byte-patch the game binaries

> **Off the table, permanently.** Do not edit a single byte of `ddctd_cm_yo_server.exe`,
> `yo_cm_client.exe` (a.k.a. `LiF.exe`), or any shipped compiled artifact — including `.cs.dso`
> compiled-script files — to change behaviour. This holds **even for a single-byte change that
> someone else has already proven works**.

### Why

- **Steam file-verify clobbers edits.** A verified binary is overwritten on the next integrity check, so a byte-patch is not a durable deployment anyway.
- **No update story.** The server is a fork of Torque3D; the client and server are versioned, signed Steam content. A hard-coded offset patch breaks silently on any rebuild — whereas an injected hook is anchored to a function we can re-locate (see [`reverse_engineering.md`](reverse_engineering.md)).
- **It's the project's stated line.** Binary patching of the game executables is a hard "no" from the project lead, regardless of how small or how well-tested the edit is.

### The sanctioned alternatives

| Path | Allowed? | What it is |
|---|---|---|
| **DLL injection** (`pdh.dll` proxy + Microsoft Detours) | ✅ canonical | Get an LiFx DLL into the process and hook the engine function at runtime. Server side is documented in [`loader_and_injection.md`](loader_and_injection.md); the *same* phantom-DLL-hijack pattern is the intended mechanism for client-side changes too. |
| **Loose `.cs` script** dropped into `scripts/` (e.g. `scripts/client/`) | ✅ low-cost only | The engine already searches for loose source files, so adding one is *not* an exe edit. Cheap for TorqueScript-layer additions. **Caveat:** Steam's file-verify may clobber it, so it is unsuitable for anything load-bearing. |
| **Editing a `.cs.dso`** (compiled script) | ❌ prohibited | A `.dso` is a shipped *binary* file; editing it is binary patching under the same rule. |
| **Byte-editing the exe** | ❌ prohibited | The rule, full stop. |

### Durable script changes without touching the `.dso`

If a script change must survive (i.e. a loose `.cs` is too fragile and the `.dso` is off-limits), have
the **injected DLL run the TorqueScript at startup** via the engine's evaluate entry point rather than
editing the compiled script:

- `CON_EVALUATE = 0x406A50` (`Con::Evaluate`, named constant in [`cm_offsets.h`](../source/server/cm_offsets.h)) — call this from the injected DLL to inject TS at boot.
- The injected DLL's presence is already observable: every engine console line is prefixed `[LiFx]` via the `CON_INTERNAL_PRINTF = 0x405090` (`Con::InternalConsolePrintf`) hook. Seeing `[LiFx]` on the console means the whole Detours chain is live (proxy → mod DLL → printf hook), so a startup `Con::Evaluate` will have run too.

### How injection gets in (server side, for reference)

`ddctd_cm_yo_server.exe` statically imports four symbols from `pdh.dll`
(`PdhOpenQueryW`, `PdhCollectQueryData`, `PdhAddCounterW`, `PdhGetFormattedCounterValue`); `pdh.dll`
is not an x64 KnownDLL, so a `pdh.dll` dropped next to the exe is loaded in preference to the system
one. That proxy forwards the four symbols to `System32\pdh.dll`, then `LoadLibraryW`s the actual mod
DLL (current opaque name `4ba5cb5e.dll`). Full mechanism, deployment, and Wine `WINEDLLOVERRIDES`
notes live in [`loader_and_injection.md`](loader_and_injection.md). The client-side equivalent reuses
the identical phantom-proxy + Detours shape against the client process.

### Worked example — the 9x9 grid clamp

The clearest application of this rule. A community find (Pabluuz, Feb 2025) lifts the client's world-grid
clamp with a **single-byte edit at file offset `0x189554` in `yo_cm_client.exe`, `0x03` → `0x09`**
(opens the default 3x3 grid envelope to 9x9). We record the clamp's *location* because we want to
**hook it from an injected client DLL**, but we do **not** apply the byte-patch in production. Note
`0x189554` is a **file offset into the client image**, not a server RVA — it is *not* in `cm_offsets.h`
(grep confirms no match; that file catalogues *server* RVAs relative to image base `0x140000000` only).
Full analysis: [`client_grid_patch.md`](client_grid_patch.md).

> Client vs. server addresses: `cm_offsets.h` does carry one client RVA as a comment —
> `CLIENT_OPEN_FILE_STREAM` at `0x61E620` — purely to anchor the server twin
> `OPEN_FILE_STREAM = 0x44A2C0`. It is a *comment*, not a named `CmOffset` enum constant, and it does
> not make client addresses comparable to the server RVAs elsewhere in the knowledge base.

### Corollary — fail closed

Injection itself must fail loudly, not silently degrade. In `DllMain(DLL_PROCESS_ATTACH)` the proxy
resolves the real `pdh.dll` and the four forwards; if any step fails it `return FALSE`s so the exe
refuses to start. A silent partial bootstrap is the worst failure mode — prefer a loud abort. (Same
spirit: LiFx aborts startup if `config/lifxpluss.xml` is missing.)

---

## Rule 2 — Public docs describe only the TorqueScript-exposed API

> The `ghdocs/docs/` tree is the **public surface**. Mirror only what a TorqueScript author working
> against the documented API needs. Never publish engine internals, reverse-engineering, build steps,
> or the mod-DLL filename.

### Where it goes

Local `ghdocs/docs/<x>` mirrors verbatim to `docs/<x>` of the public repo:

> `lifxpluss/ghdocs/docs/<anything>` ↔ `Rampart-Games-Limited/LiFxRampart` → `docs/<anything>`

The `ghdocs/` wrapper exists solely to keep the public set cleanly separated from this private
repo-root `docs/` tree. Two audiences, two repos: **script authors** read the public mirror;
**mod maintainers** read this private knowledge base.

### What is allowed vs. forbidden

| Belongs in `ghdocs/docs/` (public) | Stays private / omitted entirely |
|---|---|
| TorqueScript-exposed commands only: `Lifx::*`, `LifxTimers::*`, `Player.lifx*`, etc. | The Detours implementation: which engine **RVAs** we hook, **vtable slot** indexes, **struct layouts** — anything RE'd from `ddctd_cm_yo_server.exe`. |
| Signatures, semantics, examples for those commands. | The **mod-DLL filename** we ship (the opaque/rotating name, e.g. `4ba5cb5e.dll`) and the loader-proxy mechanism. |
| | Build steps, internal hook source files, Ghidra evidence, raw offsets. |

### The gate test

For every sentence about to land in the public docs, ask:

> "Would a TorqueScript author working **only** against the documented API need this?"

If answering it requires the reader to look inside the DLL or know an engine internal, it does not
belong in the public set — keep it in this private knowledge base or omit it.

### Why

The two audiences have opposite needs. Script authors want a clean API reference and nothing that
leaks the binary internals; the reverse-engineering specifics and the shipped DLL name are kept
private precisely so a public reader has nothing to grep for. A distinct repo plus restricted content
keeps the boundary unambiguous and mechanical to enforce.

---

## Status & provenance

- **Policy, not RE.** Both rules are **project-lead directives** (operational policy), not runtime findings — they are stated requirements, applied as written.
- **Runtime-verified facts cited here:** the `[LiFx]` console marker via the `CON_INTERNAL_PRINTF` hook and the fail-closed proxy bootstrap are confirmed live (see [`loader_and_injection.md`](loader_and_injection.md)). `CON_EVALUATE = 0x406A50` and `CON_INTERNAL_PRINTF = 0x405090` are named constants in [`cm_offsets.h`](../source/server/cm_offsets.h).
- **Externally verified, not re-tested here:** the 9x9 grid clamp byte-patch (`0x189554`, `0x03` → `0x09` in `yo_cm_client.exe`) is a community find/test; per Rule 1 it is **recorded as a hook target, never applied**. See [`client_grid_patch.md`](client_grid_patch.md).
- **Cross-check:** `0x189554` is a *client file offset* and is correctly **absent** from `cm_offsets.h`; `0x61E620` (`CLIENT_OPEN_FILE_STREAM`) appears only as an explanatory comment there, not as a named enum constant — no conflict.
