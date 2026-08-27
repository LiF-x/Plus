---
title: Reverse engineering setup
status: reference
domain: reverse-engineering
tags: [ghidra, scripts, tooling, re]
related: [architecture.md, conventions.md, net_events.md]
updated: 2026-06-26
---

# Reverse Engineering

For finding new offsets to hook. The game is no longer patched by Bitbox, so the analysis below is stable — re-run only if you point LiFx at a different build of `ddctd_cm_yo_server.exe`.

## Why this is tractable

The LiF server is a **fork of Torque3D**, an open-source MIT-licensed engine. Most of the function names you'll want to hook (`Con::Init`, `gStringTable::Insert`, `NetEvent::pack/unpack/process`, datablock virtuals, etc.) are public in the Torque3D source on GitHub. You don't reverse from zero — you anchor against published source and use strings / RTTI to locate the same functions in the stripped binary.

Two big helpers in this specific binary:

- **RTTI is preserved.** MSVC C++ writes vtable type-info strings into `.rdata` even without a PDB. Ghidra's RTTI analyzer recovers 4,859 namespaces/classes, including all 156 `*Event` network event classes and hundreds of datablocks.
- **No stripping.** Strings (format args, error messages, SQL fragments, console command names) are all intact. Cross-referencing a known string to its using function is the most reliable identification path.

No PDB is present (release build), so functions show up as `FUN_<RVA>` rather than human-readable names — but RTTI gives us *class* names, and strings give us *behavior* anchors, which is usually enough.

## Toolchain

- **Ghidra 12.1+** at `~/.local/share/ghidra/`. Installed by downloading the official release zip from GitHub (NSA's repo) and unpacking — no `sudo`, no package manager.
- **JDK 17+.** Arch's `java-26-openjdk` is fine.

```bash
# fetch the latest official Ghidra release URL
URL=$(curl -s https://api.github.com/repos/NationalSecurityAgency/ghidra/releases/latest \
        | grep browser_download_url | head -1 | cut -d'"' -f4)
mkdir -p ~/.local/share && cd ~/.local/share
curl -L -o ghidra.zip "$URL"
unzip -q ghidra.zip && rm ghidra.zip && mv ghidra_* ghidra
~/.local/share/ghidra/support/analyzeHeadless 2>&1 | head     # smoke test
```

## Initial analysis (one-time)

```bash
mkdir -p ~/ghidra_projects
~/.local/share/ghidra/support/analyzeHeadless \
  ~/ghidra_projects LiF \
  -import /home/mjoed/LifeIsFeudal/lif_server_320850/ddctd_cm_yo_server.exe \
  -overwrite \
  -analysisTimeoutPerFile 2400
```

Takes ~3 minutes on a modern machine. Output project lives at `~/ghidra_projects/LiF.rep/`. No PDB processing (binary has none), but RTTI analyzer + x86 constant-reference analyzer do the heavy lifting.

## Extracting artifacts

Three GhidraScripts at the repo root, plus one Python post-processor:

- `scripts/ghidra/LifxExport.java` — symbols, strings with xrefs, RVA validation.
- `scripts/ghidra/LifxEventVtables.java` — walks every `*Event` class vtable, emits per-class handler tables.
- `scripts/ghidra/LifxAllRegs.java` — decompiles the four `_*BehaviorNodes::init` functions used by `parse_node_registrations.py`.
- `scripts/ghidra/LifxEffectsScan.java` — hunts xrefs to effect-parameter tokens (`SPEED`, `HARD_HP_MAX`, …) and dumps vftables of every `*_Ability` / `*Effect` / `*SpecialAttack` RTTI class. Outputs `/tmp/lifx_ghidra/effects_*.tsv`; consumed by [`effects_and_abilities.md`](effects_and_abilities.md).
- `scripts/ghidra/LifxResurrectionScan.java` — three-anchor fan-in (icon path, "Resurrected" string, imm-47/93) used to start the resurrection-sickness apply-site hunt. Outputs `/tmp/lifx_ghidra/res_*.tsv`. See [`effects_and_abilities.md`](effects_and_abilities.md) §"Resurrection sickness" for what it found and what remains open (tracking #30).
- `scripts/parse_node_registrations.py` — reads the decompiles from `LifxAllRegs.java` and produces the canonical XML-class-name → C++-class registration table documented in [`ai_and_spawning.md §3.4`](ai_and_spawning.md). Run after `LifxAllRegs.java`. Has `--tsv` for machine output.

Run either via headless `-postScript`:

```bash
~/.local/share/ghidra/support/analyzeHeadless ~/ghidra_projects LiF \
  -process ddctd_cm_yo_server.exe -noanalysis \
  -scriptPath scripts/ghidra -postScript LifxExport.java
```

(Ghidra 12.x dropped Jython, so the scripts are Java. PyGhidra would work too but requires pip + extra setup.)

Outputs land in `/tmp/lifx_ghidra/`:

| File | Size | What it gives you |
|---|---|---|
| `offset_check.txt` | 1.5 KB | The 14 RVAs in `cm_offsets.h` validated against the current binary — function entry point + prologue bytes for each. Run this after any binary update. |
| `classes.txt` | 312 KB | All RTTI-recovered namespaces/classes. The 156 `*Event` classes plus all `ConcreteClassRep<T>` registrations live here. |
| `functions.tsv` | 2.4 MB | `rva  address  name  namespace  signature` for every analyzed function (~37k). Most names are `FUN_<rva>` (no PDB); namespaces are RTTI-derived. |
| `strings.tsv` | 2.5 MB | `rva  address  len  type  value  xref_funcs` for every defined string ≥3 chars (~27k). **Column 6 is the magic** — it lists every function that references the string. Cross-reference any error/log message back to its emitting function. |
| `event_vtables.tsv` | — | Generated by `LifxEventVtables.java`. Per-slot vtable dump for all `*Event` classes (~1800 slot rows). |
| `event_classes.tsv` | — | Same script's compact summary: one row per Event class with vtable RVA and virtual-method count. |
| `event_handlers.tsv` | — | Same script's derivation: `class  pack_rva  unpack_rva  process_rva` for the ~130 NetEvent-derived classes. See [`net_events.md`](net_events.md). |

## Validation: are the existing offsets still good?

Yes — verified against the current Steam build (May 2026). Every one of the 14 entries in `cm_offsets.h` lands on a function entry point with a clean x64 prologue. From `offset_check.txt`:

```
CON_INTERNAL_PRINTF     0x405090  ->  prologue  89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 B8   (mov [rsp+10], edx; push rbp/rsi/rdi/r12-r15)
CON_INIT                0x407990  ->  prologue  48 83 EC 78 48 C7 44 24 40 FE FF FF FF C6 05 60   (sub rsp,78h; mov [rsp+40], -2)
STRING_TABLE_INSERT     0x441BF0  ->  prologue  48 8B C4 57 41 56 41 57 48 83 EC 50 48 C7 40 C8   (mov rax,rsp; push rdi/r14/r15; sub rsp,50h)
…
```

All 14 are stable. Bitbox isn't patching the game, so this snapshot should remain valid indefinitely.

## Workflow: finding a new function

### Pattern A — anchor against Torque3D source

1. Open the Torque3D source on GitHub (`https://github.com/TorqueGameEngines/Torque3D` or the older `GarageGames/Torque3D`).
2. Find the function you want. Note any distinctive log strings, `Con::warnf`/`Con::errorf` calls, error messages, or unique constant strings it uses.
3. `grep -i "your distinctive string" /tmp/lifx_ghidra/strings.tsv` — find the string's RVA.
4. Read column 6 of that row — the using function(s) by name (`FUN_<rva>`).
5. Look up that function in `functions.tsv` to confirm signature/prototype, or open it in the Ghidra GUI for the decompiler view.

### Pattern B — anchor against RTTI

For any class Torque registered with `ConcreteClassRep<T>`:

1. `grep -E "^MyClassName$" /tmp/lifx_ghidra/classes.txt` — confirm it's RTTI-recovered.
2. For `*Event` classes specifically, look it up in `event_handlers.tsv` to get the (pack, unpack, process) RVAs directly.
3. For other classes (datablocks, SimObjects, etc.), find the vtable symbol with a follow-up script or the GUI, and walk the slots the same way `LifxEventVtables.java` does.

### Pattern C — open the GUI

When string-grep and RTTI both miss, open `~/ghidra_projects/LiF.rep` in the Ghidra GUI:

```bash
~/.local/share/ghidra/ghidraRun
```

Open the project, double-click `ddctd_cm_yo_server.exe`, and use the decompiler. `g` jumps to an address; `s` searches; **Window → Function Call Graph** shows callers/callees of the current function.

## Re-validating after a binary update (just in case)

If LiF ever does get re-patched:

1. Re-download via steamcmd (see [`build.md`](build.md)).
2. Delete the old project: `rm -rf ~/ghidra_projects/LiF.{gpr,rep}`.
3. Re-run the import command above.
4. Re-run `LifxExport.java` and diff `offset_check.txt` against the previous version. Any row where `prologue_bytes` changes shape means that hook's RVA is stale and needs reassignment.
