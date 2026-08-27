---
title: Architecture
status: reference
domain: lifx-framework
tags: [lifx-framework, architecture, rscl-fork]
related: [conventions.md, loader_and_injection.md, build.md, reverse_engineering.md]
updated: 2026-06-26
---

# Architecture

## What LiFx is

A Windows x64 C++ DLL that injects into `ddctd_cm_yo_server.exe` via Microsoft Detours, patches the running image to hook engine functions at known RVAs, and exposes a console API for in-server scripting and instrumentation.

The output of the LiFx build is two DLLs that get dropped alongside the server exe:

| DLL | Built from | Role |
|---|---|---|
| `pdh.dll` | `source/loader/pdh_loader.cpp` (LiFx_Loader vcxproj) | Phantom-DLL hijack of the `pdh.dll` import. Forwards the four `Pdh*` symbols to `System32\pdh.dll`, then `LoadLibraryW`s the mod DLL (current name: **`4ba5cb5e.dll`**). |
| `4ba5cb5e.dll` | the rest of `source/` (LiFx vcxproj) | The actual mod. Installs Detours hooks on engine functions and registers a console API. |

The mod DLL's filename is **deliberately opaque** — there is no static import of it from the exe, so its name is entirely a choice of the proxy loader. We use a random 8-hex-char name so that anyone scanning the server's working directory has nothing to grep for. The name lives in three places that **must stay in sync**:

1. `LIFX_DLL_NAME` at the top of `build_linux.sh` (the linker output filename).
2. `<TargetName>` in `win/LiFx.vcxproj` (the Windows build path's output name).
3. The `LoadLibraryW(L"…")` literal in `source/loader/pdh_loader.cpp` (the only place that *resolves* the name at runtime).

To rotate the name, edit those three locations to the same string and rebuild. Nothing else in the codebase reads it.

## Fork history and naming

LiFx is a fork of the **RedShark Server Core Library (RSCL)**, copyright (c) 2023
RedShark Foundation. The framework core it derives from — and the parts that are
original LiFx work — are itemised in [`CONTRIBUTORS.md`](../CONTRIBUTORS.md).

LiFx is the primary brand and copyright holder throughout: namespace `Lifx::`, console constant `$lifx::Version`, log prefix `[LiFx]`, project files `LiFx.*`. The rename from RSCL covered `Redshark::` -> `Lifx::`, `$rscl::Version` -> `$lifx::Version`, the `[RSCL]` log prefix, and the `RSCL.*` project files.

Identifiers tied to the LiF/cm_yo engine lineage (`cm_server`, `cm_offsets`, `__CM_*` macros, the `cm_yo` namespace concept) were left alone — those name the *target* binary, not LiFx itself.

## Source-tree map

```
lifxpluss/                      ← LiFx repository
├── source/
│   ├── cm_config.h             ← version string, build-mode flags
│   ├── dllmain.cpp             ← mod DLL's DllMain (Detours transaction)
│   ├── core/
│   │   ├── cm_aux.{h,cpp}      ← Lifx::ShowInfo/Error, __CM_* macros
│   │   ├── cm_globals.{h,cpp}  ← AuxGManager (named-pointer registry, etc.)
│   │   ├── cm_memory_mgr.{h,cpp}
│   │   ├── cm_platform.h
│   │   └── tinyxml2.{h,cpp}    ← third-party (zlib license)
│   ├── loader/
│   │   └── pdh_loader.cpp      ← pdh.dll proxy + mod-DLL bootstrap (LoadLibraryW)
│   └── server/
│       ├── cm_constants.h
│       ├── cm_offsets.h        ← RVAs for engine functions to patch
│       ├── cm_server.{h,cpp}   ← Lifx::Server (Init/Stop, config load)
│       ├── cm_wrappers.h
│       ├── hooks_engine.{h,cpp}← attaches/detaches the 3 engine hooks
│       ├── api/t3d_console.{h,cpp}  ← Con::Echo/AddCommand/Evaluate wrappers
│       └── hooks/engine/hook_console.{h,cpp}  ← OnInternalPrintf hook
├── extra/                      ← Microsoft Detours headers + .lib
├── game/config/lifxpluss.xml   ← LiFx-only runtime config (consumed by Lifx::Server::Init)
├── win/
│   ├── LiFx.sln
│   ├── LiFx.vcxproj            ← builds the mod DLL (TargetName = LIFX_DLL_NAME without extension)
│   └── LiFx_Loader.vcxproj     ← builds pdh.dll
├── build_linux.sh              ← clang-cl + xwin cross-build
└── docs/                       ← this directory
```

## What's actually wired today

LiFx currently installs **three Detours hooks** (`source/server/hooks_engine.cpp::AttachHooks`):

| Hook | Engine RVA | Purpose |
|---|---|---|
| `Con::InternalPrintf` | `0x405090` | Intercept every console line the engine prints. Used to filter SQL noise, route errors/SQL into external log files, prefix every line with `[LiFx]`. |
| `Con::Init` | `0x407990` | Wait for Torque console init to complete; from inside, look up the remaining `Con::*` and `gStringTable` symbols and register `$lifx::Version`, `$cm_globals::IsYOServer`, `$cm_globals::YOVersion`, `$cm_globals::YOVersionString`. |
| `gStringTable::Insert` | `0x441BF0` | Capture the first call to grab the `gStringTable` pointer (used by `Con::AddCommand` to intern command names). |

Visible output: every engine console line is `[LiFx] <message>`, plus a one-shot `==== Powered by LiFx ver. 1.0` banner at console init. Optional external file logs under `logs/<YYYY-MM-DD>/{errors,sql}/` when `lifxpluss.xml` enables them.

## What's a framework (no body yet)

`Lifx::Server::AttachHooks()` / `DetachHooks()` in `cm_server.cpp` are **empty stubs with comments**. They're the seam where gameplay/`cm_yo`-specific hooks are meant to land — separate from the engine-level hooks in `hooks_engine.cpp` that already work.

The C++ API exposed by `source/server/api/t3d_console.{h,cpp}` is what future code should use:

| API | What you get |
|---|---|
| `Con::AddCommand(ns, name, callback, usage, minArgs, maxArgs)` | Register a TorqueScript-callable native C++ function. 5 overloads for return type (Int/Float/String/Void/Bool). |
| `Con::AddVariable(name, type, &data, usage)` | Expose a C++ value as a TorqueScript global `$name`. |
| `Con::AddConstant(name, type, &data, usage)` | Expose a read-only constant. |
| `Con::GetVariable(name)` / `Con::SetVariable(name, value)` | Read/write any TorqueScript global at runtime. |
| `Con::Evaluate(src, echo, fileName)` | Run arbitrary TorqueScript from C++. |
| `Con::Echo/Warning/Error/Info(fmt, …)` | Print to the live in-game console (bypasses the `[LiFx]` prefix hook by design — LiFx's own output stays clean). |
| `gSpace.Pointers().Set/Get(name, ptr)` | Cross-module named-pointer registry. |

## Adding a new Detours hook (idiom)

The pattern is fully established by the three existing hooks. The mechanical recipe:

1. Locate the function in `ddctd_cm_yo_server.exe` (see [`reverse_engineering.md`](reverse_engineering.md)), record its RVA.
2. Add an entry to `enum CmOffset` in `source/server/cm_offsets.h`.
3. Declare the original prototype with `__CM_DECL_INTERNAL(ret, callconv, name, args…)`, and `__CM_INSTATNTIATE(name)` in a `.cpp`.
4. Write your hook function with the same signature.
5. Call `__CM_ATTACH_HOOK(offset, original_ptr, your_hook)` from `Lifx::Server::AttachHooks()` (gameplay hooks) or `Hooks::AttachHooks()` in `hooks_engine.cpp` (engine-level), and the matching `__CM_DETACH_HOOK(…)` from the corresponding `DetachHooks()`.

The engine offsets in `cm_offsets.h` have been verified against the current Steam build (see [`reverse_engineering.md`](reverse_engineering.md) — every RVA still lands on a recognizable x64 function prologue). The game is no longer patched by Bitbox, so this should remain stable indefinitely.

## Runtime config (`lifxpluss.xml`)

LiFx aborts startup if `config/lifxpluss.xml` (relative to the server's working directory) can't be loaded. This file is **LiFx-only** — the engine has no awareness of it (its world config is `config/world_1.xml` with root `<config>`). Relevant keys, all under `<log>`:

| Key | Type | Effect |
|---|---|---|
| `level` | int | Console verbosity filter: `0` all, `1` skip info, `2` skip warnings+info, `3` errors only. |
| `skipSQLQueriesLog` | bool | Suppress lines starting with `DB::noRS`, `DB::mNoRS`, `DB::RS`, `DB::mfRS`, `DBI::`, `DB_TRAN_ADD` from the live console. |
| `enableExternalErrorLog` | bool | Duplicate engine error lines (`type==2`) to `logs/<date>/errors/S<WorldID>_<datetime>_p<pid>.log`. |
| `enableExternalSqlLog` | bool | Duplicate SQL-prefixed lines to `logs/<date>/sql/S<WorldID>_<datetime>_p<pid>.log`. |

`<WorldID>` is currently hardcoded to 1 (see `cm_server.cpp` comment `// warn: here we has 1 as WorldID`).
