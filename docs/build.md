---
title: Build
status: reference
domain: lifx-framework
tags: [build, clang-cl, xwin, lld-link]
related: [architecture.md, loader_and_injection.md]
updated: 2026-06-26
---

# Build

LiFx supports two parallel build paths:

- **Linux (recommended)** via `clang-cl` + `lld-link` + `xwin`. No Windows VM, no Wine. Produces real MSVC-ABI PE DLLs that link `extra/lib/detours.lib` unchanged.
- **Windows** via `win/LiFx.sln` in Visual Studio 2022 (toolset v143, C++20). The original build path; still intact.

Both paths produce identical artifacts: the mod DLL (current name **`4ba5cb5e.dll`** — see [`architecture.md`](architecture.md) for why it's opaque) and `pdh.dll`, both under `win/build/Release/`.

## Linux toolchain (clang-cl + xwin)

### One-time setup

```bash
sudo pacman -S clang lld llvm cargo            # Arch / CachyOS
cargo install xwin                              # installs into ~/.cargo/bin
fish_add_path ~/.cargo/bin                      # or set -gx PATH; bash equivalent in .bashrc

xwin --accept-license splat \
  --output ~/.xwin \
  --include-debug-libs
```

`xwin splat` downloads ~1–2 GB of Microsoft SDK + Visual C++ headers/libraries via the official redistributable channel (legal, same source VS itself uses) and lays them out in a layout `clang-cl` can consume. Result lives in `~/.xwin/` (`~3 GB` on disk).

### Build

```bash
./build_linux.sh                # Release, both DLLs
./build_linux.sh loader         # just pdh.dll
./build_linux.sh lifx           # just the mod DLL (alias: cm_server)
LIFX_DLL_NAME=foo.dll ./build_linux.sh lifx   # override the output filename
BUILD=Debug ./build_linux.sh    # debug build with /Z7 line info
```

Output lands in `win/build/Release/` (or `win/build/Debug/`).

### What the script does

`build_linux.sh` invokes `clang-cl --target=x86_64-pc-windows-msvc` with:

- `/imsvc` for each xwin include subtree (`crt/include`, `sdk/include/{ucrt,um,shared,winrt}`).
- `/libpath:` for `crt/lib/x86_64`, `sdk/lib/ucrt/x86_64`, `sdk/lib/um/x86_64`, plus `extra/lib` for `detours.lib`.
- `-fuse-ld=lld` so the linker is `lld-link` (without this, clang-cl tries to spawn `link.exe` which doesn't exist on Linux).
- `-Wno-msvc-not-found` to silence the harmless autodetection warning (we override every search path).
- `/MD /O2` Release (or `/MDd /Od /Z7` Debug), `/std:c++20 /EHsc /W3`, plus the same preprocessor defines the vcxproj uses (`_WIN32_WINNT=0x0A00`, `WIN32_LEAN_AND_MEAN`, `_YOUR_OWN_AURORA`, etc.).

### Known compile-time tweaks (already applied)

These were patched in the existing source to make clang accept what MSVC was lenient about:

- `sizeof MODULEINFO` → `sizeof(MODULEINFO)` in `cm_memory_mgr.cpp:67,71` and `hooks_engine.cpp:87`. The unparenthesized form is invalid C++; MSVC's lenient parsing accepted it, clang doesn't.
- `__declspec(dllexport)` on the pdh proxy stubs replaced with `#pragma comment(linker, "/EXPORT:Name")` directives, because the `<Pdh.h>` declarations don't have `dllexport` and adding it to redeclarations is a clang warning.

### Persistent warnings (load-bearing, leave alone)

The Microsoft Detours macros include `(PVOID&)fnHook` casts that mix function pointers and object pointers — strictly UB in standard C++, accepted in MS-extension mode. Clang flags them as `-Wmicrosoft-cast` warnings. These are how Detours works; do not "fix" them. To silence cosmetically, add `-Wno-microsoft-cast` to the build script.

## Windows toolchain (Visual Studio)

Open `win/LiFx.sln` in Visual Studio 2022 (Community is fine). The solution has two projects:

- **LiFx** — builds the mod DLL. RootNamespace `LiFx`; `<TargetName>` is the DLL basename without extension (currently `4ba5cb5e`). Detours linked from `extra/lib/detours.lib`. To rotate the name on the Windows path, edit `<TargetName>` in both `Debug|x64` and `Release|x64` PropertyGroups and keep it in sync with `LIFX_DLL_NAME` in `build_linux.sh` and the literal in `source/loader/pdh_loader.cpp`.
- **LiFx_Loader** — builds `pdh.dll`. RootNamespace `LiFx_Loader`, TargetName `pdh`. No Detours dep.

Both target Debug|x64 and Release|x64, v143 toolset, C++20. Build → outputs to `win/build/<Config>/`.

## Deploy

The output DLLs must be copied to the server install dir each time you rebuild:

```bash
cp win/build/Release/{pdh,4ba5cb5e}.dll /home/mjoed/LifeIsFeudal/lif_server_320850/
```

If you only changed mod-DLL source, you only need to copy the mod DLL. The `pdh.dll` proxy is small and changes rarely (rebuild only when rotating `LIFX_DLL_NAME`).

See [`loader_and_injection.md`](loader_and_injection.md) for the full deployment list including `lifxpluss.xml`.

## Re-downloading the dedicated server (if needed)

```bash
steamcmd \
  +@sSteamCmdForcePlatformType windows \
  +force_install_dir /home/mjoed/LifeIsFeudal/lif_server_320850 \
  +login anonymous \
  +app_update 320850 validate \
  +quit
```

Without `@sSteamCmdForcePlatformType windows`, steamcmd selects the Linux depot and you get only stub `.so` files — no Windows binaries. The manifest will misleadingly report "fully installed".
