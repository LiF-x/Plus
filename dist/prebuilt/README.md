# Prebuilt LiFx server DLLs

Both compiled LiFx server binaries, so collaborators can run the current build without
the cross-compile toolchain (clang-cl + lld-link + xwin). **You need both** — `pdh.dll`
is a proxy that the game loads, and it in turn loads the `4ba5cb5e.dll` extension.

| File | What it is | Build |
|---|---|---|
| `pdh.dll` | The loader/proxy. The server loads `pdh.dll` (a real system DLL name it imports); our proxy then `LoadLibraryW`s `4ba5cb5e.dll`. Rarely changes. | `./build_linux.sh loader` |
| `4ba5cb5e.dll` | The LiFx server extension (all the hooks: bandit combat, etc.). | `./build_linux.sh lifx` |

- **Built from:** `main` (the #154 bandit combat: held weapon, `endAttack` contact-frame
  damage, stamina pacing, swing protection — see [`docs/bandit_combat.md`](../../docs/bandit_combat.md)).
- **Deploy:** copy **both** to the server root (`pdh.dll` + `4ba5cb5e.dll`). See
  [`docs/loader_and_injection.md`](../../docs/loader_and_injection.md) for the Wine
  `WINEDLLOVERRIDES` setup the proxy needs.
- **Rebuild both:** `./build_linux.sh all` (loader + lifx + client), or each target above.

> These binaries are a convenience snapshot and may lag `main`. When in doubt, rebuild from
> source and refresh with `cp win/build/Release/{pdh,4ba5cb5e}.dll dist/prebuilt/`.
