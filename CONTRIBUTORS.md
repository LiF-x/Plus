# Contributors

Who wrote what in this repository, and which parts rest on outside work.

Copyright in the source headers is held by **LiFx Contributors**; this file records
provenance behind that collective name. The project is licensed under
[GPL-3.0](LICENSE).

## Upstream basis — RedShark Foundation

LiFx began as a fork of the **RedShark Server Core Library (RSCL)**,
copyright © 2023 RedShark Foundation. The framework core — process attach, the
memory manager, the platform/type layer, the Torque console bridge, and the
engine hook scaffolding — derives from RSCL. Internal identifiers were renamed
during the fork (`Redshark::` → `Lifx::`, `$rscl::Version` → `$lifx::Version`,
log prefix `[RSCL]` → `[LiFx]`, project files `RSCL.*` → `LiFx.*`), but the
lineage is RSCL's.

These twenty files are the RSCL-derived core:

| Area | Files |
| --- | --- |
| Core runtime | `source/core/cm_aux.{h,cpp}`, `source/core/cm_globals.{h,cpp}`, `source/core/cm_memory_mgr.{h,cpp}`, `source/core/cm_platform.h` |
| Entry point & config | `source/dllmain.cpp`, `source/cm_config.h` |
| Server layer | `source/server/cm_server.{h,cpp}`, `source/server/cm_wrappers.h`, `source/server/cm_constants.h`, `source/server/cm_offsets.h` |
| Console bridge | `source/server/api/t3d_console.{h,cpp}`, `source/server/hooks/engine/hook_console.{h,cpp}` |
| Hook scaffolding | `source/server/hooks_engine.{h,cpp}` |

Everything else in `source/` is original LiFx work.

## Original LiFx work

The following are **not** derived from RSCL and are not attributable to any
outside contributor:

- **Loader and injection** — `source/loader/pdh_loader.cpp` and the `pdh.dll`
  proxy approach, including the DllMain-timing analysis behind it.
- **Encrypted assets (LFXE)** — `source/core/crypto/` in full: the ChaCha20
  container format, the FileStream decrypt seam, and the key path.
- **Client-side work** — everything under `source/client/`.
- **Gameplay and engine reverse engineering** — the character/combat, AI,
  furnace and craftwork, effects, outpost, battlezone, netevent, dispatcher and
  sector-handoff hooks, together with the RVA and ABI research in `docs/` that
  made them possible.

## Contributors

**Pabluuz** — reverse engineering and implementation of four configurable
server hooks, plus the analysis documenting them:

- `source/server/hooks/engine/hook_recipe_starting_tools.{h,cpp}` —
  StartingToolsID-aware recipe selection
- `source/server/hooks/engine/hook_gem_drop.{h,cpp}` — gem roll probability and
  weighted item table
- `source/server/hooks/engine/hook_tunnel_drop.{h,cpp}` — extra tunnel-dig drops
- `source/server/hooks/engine/hook_tree_drop.{h,cpp}` — per-species felled-tree
  drops
- `docs/offsets.md` — RVA encyclopedia for the verified 1.4.4.5 server image
- `docs/farming.md` — `Harvest Crops` quantity and quality formulas

## Adding to this file

When code from outside the project lands here, record it in two places: a credit
line in the file header (see [`docs/conventions.md`](docs/conventions.md#file-header-copyright))
and an entry above naming the specific files. Keep the header notice itself
unchanged — this file supplements it, it does not replace it.
