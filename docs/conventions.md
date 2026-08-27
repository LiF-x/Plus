---
title: Conventions
status: reference
domain: conventions
tags: [conventions, hooks, naming, detours]
related: [contributing.md, architecture.md, reverse_engineering.md]
updated: 2026-06-26
---

# Conventions

Project-wide conventions for LiFx contributors. Keep this short and concrete.

## Hook naming

LiFx hooks engine functions via Microsoft Detours. Every hook needs three names: the trampoline (which holds a pointer to the engine's original function), the handler (LiFx's override that gets called instead), and the offset constant (where the engine function lives in `ddctd_cm_yo_server.exe`).

| Symbol | Form | Example | Notes |
|---|---|---|---|
| **Trampoline** (global variable) | `_<EngineSubsystem>_<FunctionName>` | `_Furnace_LookupProcDesc`, `_Engine_Con_InternalConsolePrintf`, `_Engine_StringTableInsert` | Lives at file scope because the `__CM_*` macros put it there. The `<EngineSubsystem>` part is **mandatory** because there's no namespace to disambiguate at global scope. Pick a subsystem name that reflects where the function lives in the engine's source layout (`Engine` for Torque core, `Furnace` for craftwork, `NPC` for AI, etc.), not where the hook lives in LiFx. |
| **Handler** (our override) | `Hooks::<Subsystem>::<FunctionName>` | `Hooks::Furnace::ProcDescLookup`, `Hooks::Engine::OnInternalPrintf` | Namespace supplies the context; function name describes the behavior. The namespace's `<Subsystem>` should match the trampoline's `<EngineSubsystem>` part. |
| **Offset constant** in `cm_offsets.h` | `<SUBSYSTEM>_<FUNCTION>` | `FURNACE_PROC_DESC_LOOKUP`, `CON_INTERNAL_PRINTF`, `STRING_TABLE_INSERT` | The `CmOffset::` enum scope is already there at use sites, but spell out the subsystem when the bare function name alone would be ambiguous. |

### Why the prefix is mandatory on the trampoline

The trampoline exists at file/global scope (the `__CM_DECL_EXTERNAL` / `__CM_INSTATNTIATE` macros expand to a free-standing `<name>_Fn <name>;`). Without the subsystem prefix, a name like `_ProcDescLookup` could mean any "process-descriptor lookup" anywhere in the engine. The handler, by contrast, is namespaced inside `Hooks::Subsystem::` so the namespace already carries the context — its function name can be short.

The existing engine hooks set the pattern: every trampoline in `hooks_engine.cpp` and friends has the form `_<Subsystem>_<Function>`. Match that.

### Recipe for adding a new hook

1. Find the function in `ddctd_cm_yo_server.exe` — see [`reverse_engineering.md`](reverse_engineering.md). Record its RVA.
2. Add an offset constant to `enum CmOffset` in `source/server/cm_offsets.h`. Name: `<SUBSYSTEM>_<FUNCTION>`.
3. Create the hook files under `source/server/hooks/<subsystem>/hook_<name>.{h,cpp}`. In the header:
   - `__CM_DECL_EXTERNAL(<retType>, __fastcall, _<Subsystem>_<Function>, <args>);` — the trampoline declaration.
   - `namespace Hooks::<Subsystem> { <retType> <Function>(<args>); }` — the handler prototype.
4. In the cpp:
   - `__CM_INSTATNTIATE(_<Subsystem>_<Function>);` — the trampoline definition.
   - `<retType> Hooks::<Subsystem>::<Function>(<args>) { … return _<Subsystem>_<Function>(args); }` — the handler body. Start with a passthrough; layer logic on top.
5. In `cm_server.cpp`:
   - `#include "hooks/<subsystem>/hook_<name>.h"`.
   - In `Lifx::Server::AttachHooks()`: `__CM_ATTACH_HOOK(CmOffset::<SUBSYSTEM_FUNCTION>, _<Subsystem>_<Function>, Hooks::<Subsystem>::<Function>);`
   - In `Lifx::Server::DetachHooks()`: `__CM_DETACH_HOOK(_<Subsystem>_<Function>, Hooks::<Subsystem>::<Function>);`
6. Register the new `.cpp` and `.h` in both `win/LiFx.vcxproj` (the `<ItemGroup>` blocks for `ClCompile` / `ClInclude`) and `build_linux.sh` (the `srcs=(...)` array inside `build_lifx`).

### Worked example

`source/server/hooks/furnace/hook_proc_desc.{h,cpp}` is the canonical reference — it's a pure passthrough hook with full comments explaining each line. Read it first when adding a new hook.

## File-header copyright

Every source file in LiFx gets the standard MIT-style notice naming LiFx Contributors as the copyright holder. See any existing file under `source/` for the verbatim form; new files use the same template.

## Where this lives

Memory copy for AI contributors: `~/.claude/projects/-home-mjoed-LifeIsFeudal-lifxpluss/memory/feedback_lifx_hook_naming.md`. Updating one means updating the other.
