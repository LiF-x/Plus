---
title: Clean dedicated-server install
status: ops
domain: operations
tags: [steamcmd, install, runbook, wine, mariadb]
related: [build.md, loader_and_injection.md, architecture.md]
sources: [docs/lif_server_clean_install.md, build.md, loader_and_injection.md, source/server/cm_offsets.h]
updated: 2026-06-26
---

# Clean dedicated-server install

Start-to-finish, reproducible procedure for installing a **vanilla** *Life is Feudal: Your Own* dedicated server from SteamCMD on Linux, then configuring it, booting it, and verifying it. The result is a pristine, unmodified install bound to a UDP world port with its own MariaDB world DB. This is the *stock* path only — no LiFx, no `pdh.dll` proxy, no DLL overrides. For getting LiFx into the process afterward, see [`loader_and_injection.md`](loader_and_injection.md); for rebuilding/re-downloading the binaries, see the re-download note in [`build.md`](build.md).

> Scope: stock server only. Everything below is exactly and only what SteamCMD delivers plus a DB connection config. No custom DLLs, no injection, no third-party payloads.

---

## At a glance

| Item | Value |
|------|-------|
| SteamCMD download app ID | `320850` (the LiF dedicated-server *tool*) |
| `steam_appid.txt` inside the install | `290080` (the *game* app the server reports to Steam — expected, not a bug) |
| SteamCMD login | `anonymous` (no Steam account needed) |
| Platform override | **required on Linux** — server is Windows-only |
| Download size | ≈ 1.5 GB (`~1509227697` bytes) |
| Server binary | `ddctd_cm_yo_server.exe` (Windows 64-bit, `x86-64`; image base `0x140000000`) |
| Default world UDP port | `28000` (server also binds `port+1` and `port+2`) |
| Folder convention | `lif_server_320850_<letter>` (`_a`, `_b`, `_c`, …) |

---

## Prerequisites

- `steamcmd` installed and on `PATH` (`which steamcmd`).
- ~2 GB free disk for the install (~1.5 GB delivered).
- A MariaDB/MySQL server reachable from the box, with a user that holds `CREATE DATABASE`.
- A target directory following the `lif_server_320850_<letter>` convention so multiple independent installs can coexist.

---

## Step 1 — Pick a clean target directory

Choose a directory that does **not** already exist (or remove it first so the install is truly clean):

```bash
cd /home/mjoed/LifeIsFeudal
TARGET=/home/mjoed/LifeIsFeudal/lif_server_320850_c
rm -rf "$TARGET"   # only if re-doing a clean install
```

## Step 2 — Run SteamCMD (force the Windows platform)

The LiF dedicated server only ships a **Windows** depot. On Linux, SteamCMD defaults to the Linux platform and silently downloads a near-empty `~103 MB` Linux stub depot instead of the real `~1.5 GB` server. You **must** force the Windows platform:

```bash
steamcmd \
  +@sSteamCmdForcePlatformType windows \
  +force_install_dir "$TARGET" \
  +login anonymous \
  +app_update 320850 validate \
  +quit
```

- `+@sSteamCmdForcePlatformType windows` must come **before** `+force_install_dir` / `+login`.
- `validate` makes SteamCMD verify every file after download (clean, integrity-checked result).
- Expect the download to climb to `~1509227697` bytes and finish with `Success! App '320850' fully installed.`

## Step 3 — Verify the install

```bash
du -sh "$TARGET"                          # expect ~1.5 GB
ls -la "$TARGET/ddctd_cm_yo_server.exe"   # server binary must exist
cat "$TARGET/steam_appid.txt"             # reads 290080 — this is correct
```

A correct vanilla install contains, at top level: `ddctd_cm_yo_server.exe`, `config/`, `core/`, `data/`, `scripts/`, `sql/`, `main.cs`, `README.txt`, the Qt/Steam runtime DLLs, and a `steamapps/` folder with `appmanifest_320850.acf`.

---

## Step 4 — Configure the world and the database connection

The server stores each world in a MariaDB/MySQL database that it **creates and populates itself** on first launch — provided you give it a user with privileges to create databases. You do **not** import any SQL by hand ("Server does the rest", per the README).

**4a. World config.** The install ships `config/world_1.xml`. The `<ID>` (here `1`) is the **worldID**; the `<port>` (default `28000`) is the UDP port — the server also uses `port+1` and `port+2`, so open/route all three. Edit names, ports, and multipliers as desired. For additional instances, copy `config/world_1.xml` to `config/world_2.xml`, etc., each with a unique `<ID>`.

**4b. DB connection.** Copy the stock DB-config template into the server root and fill it in:

```bash
cp docs/config_local.cs ./config_local.cs
```

`config_local.cs` (Torque script) — the canonical variables:

```
$cm_config::DB::Connect::server   = "127.0.0.1:3306"; // host:port
$cm_config::DB::Connect::user     = "DBUSER";          // needs CREATE DATABASE privilege
$cm_config::DB::Connect::password = "DBPASS";
$cm_config::DB::Connect::db_name  = "lif_world_c";     // pin a UNIQUE DB name per install
```

- The stock template only sets `server` / `user` / `password`. **Add `db_name` explicitly.** The DB-name variable is `$cm_config::DB::Connect::db_name` (confirmed against the binary — `::db` is *not* read).
- If you omit `db_name`, the server falls back to a default name derived from the worldID (e.g. `lif_1`), which can collide with another install's data. Pinning a unique `db_name` keeps each install (`_a` / `_b` / `_c`) fully isolated.
- The user needs `CREATE DATABASE` rights so the server can auto-create the DB on first run.

---

## Step 5 — First run

Launch the **stock** server with **no DLL overrides**. The binary is the Windows 64-bit (`x86-64`) `ddctd_cm_yo_server.exe`:

```bash
# Windows
ddctd_cm_yo_server.exe -worldID 1
```

```bash
# Linux (under Wine — verified on Wine 11.10)
cd /path/to/lif_server_320850_c
WINEDEBUG=-all DISPLAY=:0 wine ddctd_cm_yo_server.exe -worldID 1
```

- `WINEDEBUG=-all` quiets Wine's own noise; `DISPLAY=:0` just satisfies Wine's window init — this is a dedicated server with no rendering.
- The server writes its real log to `logs/<YYYY-MM-DD>/S<id>_<host>_<timestamp>_pNN.log`.

What a healthy first boot looks like (in the server log):

```
WORLD_ID=1
CREATE DATABASE IF NOT EXISTS `lif_world_c` ...
Creating new database...
Patching database [1/2]... / [2/2]...
Validating database...
CmPatcher::attachTerrain(442) -- creating new (full load: 1)!
... NavMesh updating: N tiles left ... / NavMesh update finished!
Server is up and ready to accept connections        <-- success marker
```

On a fresh world this takes **~1–2 minutes** (schema import + world creation + navmesh generation). When you see **`Server is up and ready to accept connections`** the server is live and bound to the UDP port from the world config.

---

## Step 6 — Verify it started

```bash
# UDP port bound by the server process
ss -lnup | grep ':28000'

# Fresh DB created with the full schema (~83 tables) and no characters yet
mysql --socket=/run/mysqld/mysqld.sock -uDBUSER -pDBPASS -t -e \
  "SELECT COUNT(*) tables FROM information_schema.tables WHERE table_schema='lif_world_c';
   SELECT COUNT(*) characters FROM lif_world_c.character;"
```

A correct fresh world is **83 tables, 0 characters**, and leaves any other DBs (`lif_1`, `lif_2`, …) untouched.

**Liveness ping** (from the README): send a single byte `0x0E` (14) to the server's UDP port; a running server replies with one non-zero byte.

---

## Step 7 — Stopping

There is no documented graceful console signal for the headless server; terminate the process (`SIGTERM`, then `SIGKILL` if needed). An idle server (no players) commits in DB transactions, so a stop between ticks is safe:

```bash
pkill -TERM -f ddctd_cm_yo_server.exe
```

---

## Troubleshooting

**Install is only ~100 MB and has no `ddctd_cm_yo_server.exe`** — you got the Linux depot. The top level will show only `linux64/`, `steamapps/`, `steamclient.so`, etc. Delete the directory and re-run Step 2 with `+@sSteamCmdForcePlatformType windows`.

**`steam_appid.txt` says `290080`, not `320850`** — expected. `320850` is the SteamCMD *download* app (the server tool); `290080` is the game app the server reports to Steam.

**Other DB clients get `ERROR 2002 ... (115)` during first boot** — transient. First-boot world creation is DB-intensive and churns many short-lived connections; the `mysql` client's short connect timeout can trip (`115` = `EINPROGRESS` / timed-out handshake). The server itself is unaffected; it clears once world creation settles. Confirm the DB is actually healthy with a raw connect, e.g. `python3 -c "import socket;socket.create_connection(('127.0.0.1',3306),3)"`.

**`Can't connect to local server through socket '/home/container/run/mysqld/mysqld.sock'`** — the client's compiled-in default socket path is wrong for this host (containerized Pterodactyl-style `/home/container` paths leak into client errors). The real socket is `/run/mysqld/mysqld.sock`; pass `--socket=/run/mysqld/mysqld.sock`, or just use TCP `-h127.0.0.1`.

---

## What "clean" means here

A clean install is exactly and only what SteamCMD delivers, plus a `config_local.cs` pointing the server at its DB — no DLL overrides, no `pdh.dll` proxy, no logs from prior runs, no third-party files. Putting LiFx on top of this is a separate, additive step documented in [`loader_and_injection.md`](loader_and_injection.md) and must never leak DLL artifacts back into this vanilla baseline.

---

## Status & provenance

**`ops` runbook — runtime-verified.** The full procedure (download → configure → boot → verify → stop) was executed end-to-end against a fresh `lif_server_320850_c` install on `2026-06-07` and confirmed: forced-Windows-platform `~1.5 GB` download, Wine 11.10 boot to the `Server is up and ready to accept connections` marker in `~1–2 min`, and an isolated fresh DB of `83 tables` / `0 characters` with pre-existing worlds untouched. The `$cm_config::DB::Connect::db_name` variable (and the fact that `::db` is *not* read) was confirmed directly against the server binary.

This page carries **no reverse-engineered RVAs** — it is an operational baseline, not an RE artifact. None of its constants are `CmOffset` entries in [`source/server/cm_offsets.h`](../source/server/cm_offsets.h) (that header holds runtime-injection RVAs such as `PLAYER_APPLY_HIT = 0x0EE0F0`, `CHARACTER_LOAD_INMEM = 0x1BB290`, `DB_GET_WORLD_CONN = 0x54CAF0`), so there is nothing to name or reconcile against it. The only value shared with the RE docs is the exe image base `0x140000000`, which the [docs index](README.md) already documents as the RVA reference base.
