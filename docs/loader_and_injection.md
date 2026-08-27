---
title: Loader and injection
status: reference
domain: lifx-framework
tags: [loader, pdh-proxy, injection, wine]
related: [architecture.md, build.md, principles.md]
updated: 2026-06-26
---

# Loader and Injection

How LiFx gets itself into the LiF server process.

## The mechanism: phantom-DLL hijack via `pdh.dll`

`ddctd_cm_yo_server.exe` statically imports four symbols from `pdh.dll`:

```
PdhOpenQueryW
PdhCollectQueryData
PdhAddCounterW
PdhGetFormattedCounterValue
```

`pdh.dll` is **not** a Windows KnownDLL on x64, which means Windows' loader checks the exe's own directory *before* `C:\Windows\System32` when resolving the import by name. Drop a `pdh.dll` next to the exe and the OS loads it instead of the system one — without any cooperation from the exe, the user, an injector, or a script.

This is informally called a "phantom DLL hijack" — phantom because we're not replacing an existing file in the install dir; we're *adding* a DLL the install dir didn't ship.

## How the proxy is structured (`source/loader/pdh_loader.cpp`)

1. **Resolve the real pdh.** In `DllMain(DLL_PROCESS_ATTACH)`, build `<SystemDirectory>\pdh.dll`, `LoadLibraryW` it, and `GetProcAddress` the four target symbols into static function pointers. If any of this fails, `return FALSE` from `DllMain` — the exe will refuse to start, which is loud and easy to diagnose. (Failing closed is intentional; a silent partial bootstrap is the worst possible failure mode.)
2. **Bootstrap LiFx.** Build `<own-directory>\<modDll>` (i.e., the path of the proxy itself with the basename replaced by the opaque mod-DLL name) and `LoadLibraryW` it. This is the canonical "load another DLL from DllMain" pattern; Windows allows it because we don't wait on threads or take loader-lock-incompatible actions inside it. The mod DLL's own `DllMain` then runs and installs the Detours hooks via `gServer.Init()`. The basename literal lives in `pdh_loader.cpp` and must match `LIFX_DLL_NAME` in `build_linux.sh` and `<TargetName>` in `win/LiFx.vcxproj` — see [`architecture.md`](architecture.md) for the rationale.
3. **Export the four functions.** Each exported `Pdh*` stub is a trivial wrapper that calls through the static function pointer set up in step 1. Names are forwarded via `#pragma comment(linker, "/EXPORT:Name")` so they're emitted as plain undecorated symbols (no `__declspec(dllexport)` clash with `<Pdh.h>` prior declarations).

Why this timing is good: `DllMain` for `pdh.dll` runs after the exe image and all its other static imports are fully mapped, but **before** the exe's entry point executes. That's exactly the moment LiFx wants — the binary is in memory and its `cm_offsets` are valid, but the program hasn't started doing anything yet, so hook installation is uncontested.

## Why not just import the mod DLL directly?

Original assumption (wrong): the exe must already import the mod DLL by name for the auto-load to work. Verification by `objdump -p ddctd_cm_yo_server.exe` showed **no** import of the historical `cm_server.dll` name and no `cm_loader` symbol. The proxy approach was the actual answer — and it's why the mod DLL's filename is entirely a choice of the proxy loader, free to rotate (see [`architecture.md`](architecture.md) §"opaque").

## Deployment

Drop into `lif_server_320850/` (the directory containing `ddctd_cm_yo_server.exe`):

| File | Source | Notes |
|---|---|---|
| `pdh.dll` | `win/build/Release/pdh.dll` | The phantom proxy. |
| `4ba5cb5e.dll` (= `$LIFX_DLL_NAME`) | `win/build/Release/4ba5cb5e.dll` | LiFx itself. Opaque filename — see [`architecture.md`](architecture.md). |
| `config/lifxpluss.xml` | `game/config/lifxpluss.xml` | Required — LiFx aborts startup if missing. LiFx-only; the engine does not read this file. |

```bash
cp win/build/Release/{pdh,4ba5cb5e}.dll /home/mjoed/LifeIsFeudal/lif_server_320850/
mkdir -p /home/mjoed/LifeIsFeudal/lif_server_320850/config
cp -n game/config/lifxpluss.xml /home/mjoed/LifeIsFeudal/lif_server_320850/config/
```

Remember: every time you rebuild the mod DLL or `pdh.dll`, you must re-copy them into `lif_server_320850/`. The Steam install dir doesn't know about the LiFx source tree.

## Running under Wine

Wine ships its own builtin `pdh.dll` stub. By default Wine's load order for "system" DLLs is `builtin,native` — so even if your proxy is sitting next to the exe, Wine ignores it.

Override per-launch:

```bash
WINEDLLOVERRIDES="pdh=n,b" wine ddctd_cm_yo_server.exe
```

- `n,b` = native preferred, builtin fallback. `n` alone works too but is more fragile if anything else in the process pokes `pdh`.
- Persistent equivalent: `winecfg` → *Libraries* → add `pdh` → "Native then Builtin". Or `wine reg add 'HKCU\Software\Wine\DllOverrides' /v pdh /d native,builtin`.

What happens then: Wine's loader sees the import, the override says "native first", finds your `pdh.dll` next to the exe, runs *its* DllMain. The DllMain does `LoadLibraryW("C:\\windows\\system32\\pdh.dll")` — that explicit path bypasses the override and loads Wine's builtin pdh (sufficient because we only need the four `Pdh*` symbols to satisfy forwards; their numeric output doesn't matter). Then `LoadLibraryW(L"<modDll>")` (currently `4ba5cb5e.dll`) brings LiFx in. Detours hooks install via Wine's `VirtualProtect` / `FlushInstructionCache` implementations (fully supported).

### Wine version notes

- Use a 64-bit prefix (`WINEARCH=win64`).
- Wine ≥9.x recommended; earlier versions had less-mature `VirtualProtect`-on-loaded-image behavior, which is what Detours pokes.

## Verification

Quick "did everything load" check, no LiFx code change required:

```bash
WINEDLLOVERRIDES="pdh=n,b" WINEDEBUG=+loaddll wine ddctd_cm_yo_server.exe 2>&1 \
  | grep -iE 'pdh|4ba5cb5e'
```

A healthy bootstrap shows three lines (substitute the current `$LIFX_DLL_NAME` for `4ba5cb5e.dll`):

```
trace:loaddll:build_module Loaded L"…\\pdh.dll" at 0x…: native              ← our proxy got picked
trace:loaddll:build_module Loaded L"C:\\windows\\system32\\pdh.dll" at 0x…: builtin   ← real pdh for forwards
trace:loaddll:build_module Loaded L"…\\4ba5cb5e.dll" at 0x…: native         ← LiFx itself
```

If `pdh.dll` says `builtin`, the `WINEDLLOVERRIDES` didn't apply. If you see `pdh.dll : native` but no mod-DLL line, the proxy couldn't `LoadLibrary` it — check the mod DLL is adjacent to `pdh.dll`, that the filenames are an exact match between the proxy literal and the file on disk, and that its imports resolve (`objdump -p win/build/Release/4ba5cb5e.dll | grep "DLL Name"`).

For a visible-while-running marker that LiFx is live, every engine console line gets prefixed with `[LiFx]` (see `hook_console.cpp`). If you see `[LiFx]` on console output, the entire Detours chain is up — proxy → cm_server → InternalPrintf hook installed.

## Other auto-loadable DLLs (alternatives, not currently used)

If we ever need to *add* a second auto-loaded module without disturbing `pdh.dll`, the non-system imports of the exe are all candidates because each is resolved from the exe's own directory:

| DLL | Bundled? | Exports (approx) | Notes |
|---|---|---|---|
| `Qt5Core.dll` | yes | 14,634 | Huge surface for a proxy. |
| `Qt5Network.dll` | yes | 2,656 | Large surface. |
| `libmariadb.dll` | yes | 448 | Manageable; well-documented C API. |
| `steam_api64.dll` | yes | 2,138 | Classic mod-hijack target. |
| `icudt57.dll` | yes | **2** | Cleanest proxy target — only 2 exports. |

For "modify" paths, `icudt57.dll` is the easy choice. For another "add" path like `pdh.dll`, candidates are non-bundled, non-KnownDLL imports — verify case-by-case with `objdump -p ddctd_cm_yo_server.exe | grep "DLL Name"`.
