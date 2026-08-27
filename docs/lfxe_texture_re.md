---
title: Universal LFXE hook RE
status: re
domain: reverse-engineering
tags: [lfxe, filestream, textures, hook, re]
related: [dts_encryption.md]
updated: 2026-06-26
---
# M1 — universal LFXE hook: RE findings & design decision (issue #116)

Reverse-engineering to decide **design A** (hook one engine file-open that
every asset path shares) vs **design B** (Win32 catch-all:
`CreateFileMappingW`/`MapViewOfFile`/`ReadFile`/…). Tooling:
`scripts/client_re_texture.py` (API call-site + anchor scan) and
`scripts/client_re_callgraph.py` (reverse/forward rel32 call graph), both
pure-`struct`, same method as `scripts/client_re_dts.py`.

## What the binary says

Live client `yo_cm_client.exe` (24,607,688 bytes) and dedicated server
`ddctd_cm_yo_server.exe`.

1. **The memory-map hypothesis is wrong for engine assets.** The only
   functions that call `CreateFileMappingW`/`MapViewOfFile(Ex)` live in
   isolated high-RVA clusters (`0xbf2xxx`, `0xd7axxx`, `0x301xxx`) that are
   middleware (PhysX/sound/allocator) — they never reach `openFileStream`
   and never touch the texture loaders. `GFXStreamableTextureManager`
   (`0x6c59b0`–`0x6c7e10`) calls **no** mapping API, no `ReadFile`, and no
   `openFileStream`; it operates on already-loaded data + the GFX device.
   So Atlas DDS and UI PNG are **not** memory-mapped. Design B's whole
   reason to exist (lazy-decrypt a live mapping) does not apply.

2. **There is exactly one `FileStream` class, and every real-file read
   funnels through its `open` method.** `openFileStream` (client
   `0x61e620`, server `0x44a2c0`) is just **one of 5** FileStream
   constructors; it loads the FileStream vtable (client `0xff9ba0`, server
   `0x871ae8`) and calls slot 12 = `FileStream::open`. That `open` method
   (client `0x61e9c0`, server `0x44a4f0`) is called **directly from 62
   sites (client) / 23 sites (server)** spanning the whole engine —
   including the bitmap/DDS/texture/material loaders (`0x732xxx`–`0x739xxx`,
   `0x585xxx`–`0x594xxx`) — **plus** indirectly via the vtable.

3. **Why the current `openFileStream` factory hook (#115) misses textures
   and PNGs:** those loaders call `FileStream::open` *directly* (resolved
   statically, `E8 rel32`), bypassing the `openFileStream` factory we
   wrapped. The imposter DDS worked only because its loader happens to go
   through the factory. The low-level read methods (`0x718e00`, `0x980ce0`,
   `0x981f00`, …) have **no direct callers** — they are FileStream vtable
   methods reached both virtually and (for concrete callers) statically.

## Decision: design A, at the method level

Hook the **`FileStream` class methods**, not the `openFileStream` factory.
One engine class = every asset (DTS, DSO, DDS, PNG, anything) with a single
seam. No Win32 hooking, no `MapViewOfFile` handling (disproven above).

Because many callers use a concrete `FileStream` and call its methods
**statically** (not through the vtable), a vtable swap or an in-place
"morph to MemStream" is unsafe (a static `read`/dtor call would bypass it).
The robust approach is to **Detour the concrete methods** and keep the
object a real FileStream, post-processing the bytes:

- `FileStream::open(self, path, mode)` — run original; if read-mode and the
  first 24 bytes are `LFXE`, record per-object state `{nonce, plainSize}`
  in a mutex-guarded `map<self,*>` (textures load on worker threads).
  Magic-gated: non-encrypted files get no entry.
- `FileStream::_read(self, n, dst)` — if `self` has state: read ciphertext,
  then ChaCha20-decrypt `dst[0..n)` keyed by the **plaintext** byte offset
  (`filePos − 24`). ChaCha20 is byte-seekable, so arbitrary unaligned
  reads are exact.
- `getStreamSize` → original `− 24`; `getPosition` → original `− 24`;
  `setPosition(p)` → original `(p + 24)`.

Everything not in the state map tail-calls the original (one hot-path map
lookup that misses). This **subsumes** the DTS, DSO, and `openFileStream`
hooks; those can be retired (M6).

## Offsets (both binaries verified by disassembly)

| symbol | slot | client RVA | server RVA |
|---|---|---|---|
| FileStream vtable | — | `0xff9ba0` | `0x871ae8` |
| `FileStream::open` | 12 (`+0x60`) | `0x61e9c0` | `0x44a4f0` |
| `FileStream::_read` | 2 (`+0x10`) | `0x61de40` | `0x449b40` |
| `getPosition` | 6 (`+0x30`) | `0x61e910` | `0x44a470` |
| `setPosition` | 7 (`+0x38`) | `0x61ebc0` | `0x44a6f0` |
| `getStreamSize` | 8 (`+0x40`) | `0x61e930` | `0x44a490` |
| scalar-dtor | 0 | `0x61dd50` | `0x449a50` |
| `openFileStream` (old seam) | — | `0x61e620` | `0x44a2c0` |

## FileStream is buffered (key M2 input)

Disasm of `getPosition` (`0x61e910`) and `setPosition` (`0x61ebc0`) shows
FileStream is a **buffered** stream over an inner FileObject at `self+0x18`:

| field | meaning |
|---|---|
| `+0x18` | inner FileObject (real OS file; its vtable `+0x48`=getPos, `+0x50`=setPos) |
| `+0x2020` (q) | buffer-window base file offset (`-1` ⇒ no valid buffer) |
| `+0x2028` (q) | current position |
| `+0x2030` (q) | buffer-window end |
| `+0x2038` (b) | dirty flag |

`getPosition` returns `[+0x2028]` when a buffer is loaded, else delegates to
the inner object. So a naive "add 24 to every position" must be applied at
**one** layer or the buffer-window arithmetic desyncs.

### M2/M3 approach (built): decrypt-to-memory + accessor detours

The inline buffer at `self+0x20` is only **8 KB** (mask `0xe000`, refilled per
window) — so a multi-MB texture can't be parked in-place, which rules out the
"retarget the object's buffer fields" idea for large files. And because many
of the 62 callers hold a *concrete* `FileStream` and call its methods
**statically** (not through the vtable), a vtable swap or object morph would
be bypassed by those static calls.

So the implemented design (`source/core/crypto/lfxe_filestream.cpp` + the
per-DLL `hook_filestream.cpp`) **Detours the concrete methods** and keeps the
object a real FileStream:

- `open` → run original; on `LFXE` magic (read mode), decrypt the **whole**
  file into a heap buffer and register it in a mutex-guarded
  `unordered_map<FileStream*, {plaintext, pos}>`. Non-LFXE / failed / non-read
  opens clear any stale entry (FileStream objects are pooled/reused).
- `_read` / `getStreamSize` / `getPosition` / `setPosition` → if the object is
  registered, serve from the plaintext buffer (the engine's own buffering is
  bypassed for that stream); otherwise tail-call the original.
- `~FileStream` → drop the entry and free the buffer, then call the original.

Detouring the method *addresses* catches both static and virtual calls.
Per-entry data is touched only by the stream's owning thread, so after the
locked map lookup the read/seek path runs lock-free (node-based map ⇒ stable
references across rehash). Magic-gated: a vanilla file costs one map-miss per
op. Validated off-engine by `scripts/test_lfxe_filestream.sh` (full reads,
unaligned seeks, EOF edges, >8 KB payloads, passthrough).

### Milestones

- [x] M1 — RE & A-vs-B decision
- [x] M2 — design (decrypt-to-memory + accessor detours; in-place retarget
  ruled out by the 8 KB buffer + static-call callers)
- [x] M3 — implement (client + server; shared core)
- [x] M4 — packer `--ext dts,dso,dds,png` (already generic; smoke-tested)
- [x] M5 — runtime verified in the live client: encrypted PNG icons, a 5.5 MB
  DDS texture, and a DTS shape all decrypt in-memory and render, plus an
  encrypted server `main.cs` decrypts and executes on the dedicated server.
  Required a client-only fix — see below.
- [x] M6 — docs + retire the per-format hooks (`hook_dts`, `hook_dso`,
  `lfxe_stream` deleted; superseded offsets annotated in the headers)

## Client boot-crash root cause (M5): `std::mutex` → SRWLOCK

The first integration of this hook crashed the **client** on boot (the server
was unaffected). Bisected in the live client with a crash-resilient file log:
the hooks attached and fired fine through `FileStream::open`, but the process
died on the **very first registry lock** — `std::lock_guard<std::mutex>` in
`FsUnregister`.

The decisive clue: `gMap.size()` (header-only, inlined) worked, but
`gMx.lock()` faulted. `std::mutex::lock()` calls *out* to `MSVCP140.dll`'s
`_Mtx_*` exports, and in the client process's Wine/CRT context that path
faults; the dedicated server ships its own native CRT DLLs and never hit it.

Fix: the registry lock in `source/core/crypto/lfxe_filestream.cpp` now uses a
Win32 **SRWLOCK** (KERNEL32, constant-initialized, no global ctor, no MSVCP140
dependency) on Windows, falling back to `std::mutex` only for the off-engine
g++ self-test. The client DLL no longer references any `_Mtx`/`std::mutex`
symbols. Lesson for future injected-client code: prefer KERNEL32 sync
primitives over `std::mutex` to avoid the client's fragile MSVCP140 surface.
