---
title: Jorvik mod install + bug fixes
status: ops
domain: operations
tags: [jorvik, mod-install, lifx-framework, data-export, modpack]
related: [server_clean_install.md]
sources: [server_clean_install.md, source/server/cm_offsets.h]
updated: 2026-06-26
---

# Jorvik mod install + bug fixes

Start-to-finish, runtime-validated procedure for installing the **LiF-x/Jorvik1-2** mod pack
on a clean Life is Feudal: Your Own dedicated server, generating the DB data export, and
building the Yo Launcher client modpack — plus the nine real mod bugs found and fixed during
the install (all shipped upstream in **Jorvik1-2 v2.1.0**, PR #7, closes #6). This page is a
pure-script / SQL / data-pipeline runbook: the LiFx framework hooks in via TorqueScript only,
so there are **no engine RVAs and no binary patching anywhere in this procedure**. If you start
from the v2.1.0 release you get corrected source plus a pre-built `modpack.zip` and can skip the
bug-hunting; this page remains the reference for how the pipeline works and what the bugs were.

---

## Moving parts

| Piece | What it is | Source |
|---|---|---|
| **LiFx framework** | `art.zip` — the autoloader mods plug into. The real prerequisite. | `art.zip` asset of [ServerAutoloader **v4.3.0**](https://github.com/LiF-x/ServerAutoloader/releases/tag/v4.3.0) |
| **Jorvik1-2 mod** | The mod pack (server mods + client-modpack source) | `LiF-x/Jorvik1-2` **repo** (clone it — there is **no** release) |

**How the framework hooks in:** the LiF engine mounts `art.zip` at the virtual `art/` path, so
the framework's `art/main.cs` (38 KB) overrides the vanilla boot and adds the autoloader — a
pure-script hook, no DLL and no byte edits. This is why `art.zip` is **never extracted**.

> The upstream `Jorvik1-2/README.md` is misleading: wrong download repo, missing
> framework/prereq steps, missing the `createDataXMLS` export step, missing the `yolauncher/`
> deploy, and no mention of the double-boot or the export-copy step. The procedure below
> corrects all of that and was validated on a clean install (see
> [`server_clean_install.md`](server_clean_install.md)).

---

## Prerequisites

- A working LiF dedicated server with a configured DB (clean-install guide). The DB user needs
  `CREATE DATABASE` rights.
- `gh`/`git` and a zip tool (`zip`, `7z`, or Python's `zipfile`).

---

## Install procedure

### Step 1 — Get the framework (`art.zip`)

```bash
gh release download v4.3.0 --repo LiF-x/ServerAutoloader --pattern 'art.zip'
```

`art.zip` contains `main.cs`, `jettison.cs`, `utility.cs`, `sha256.cs`, `AutoloadConfig.cs`,
`dump.sql`. **Do not extract it.**

### Step 2 — Get the mod (from the repo, NOT the README's link)

The README says to download from `LiF-x/JorvikMod/releases/latest` — **that is the wrong, old
repo.** The correct source is the `Jorvik1-2` repo, which has no release:

```bash
gh repo clone LiF-x/Jorvik1-2
```

### Step 3 — Deploy to the server root

```bash
SRV=/path/to/server      # the folder with ddctd_cm_yo_server.exe

# Framework: drop art.zip in the server root — DO NOT extract (engine mounts it at art/)
cp art.zip "$SRV/"

# Mod: the repo root mirrors the server root. Deploy ALL of these:
cp -r Jorvik1-2/mods/*       "$SRV/mods/"          # server-side mod code (JorvikMod, JorvikModv2)
cp -r Jorvik1-2/data/*       "$SRV/data/"          # modded data xmls (overwrites vanilla — back up first)
cp -r Jorvik1-2/art/*        "$SRV/art/"           # heraldry symbols (merges)
cp -r Jorvik1-2/yolauncher   "$SRV/yolauncher"     # REQUIRED on the server too (see note)
```

> **Why `yolauncher/` must go on the server:** the mod's object datablocks reference 3D shapes
> at `yolauncher/modpack/mods/Jorvik/art/models/...`, and `JorvikMod2::loadDatablocks` execs
> `yolauncher/modpack/mods/Jorvik2/art/datablocks/Transport.cs`. Omitting `yolauncher/` causes
> `Unable to load shape: ...Jorvik...dts` errors and the new objects won't load.

### Step 4 — First boot (register the mod into the DB)

```bash
cd "$SRV"
ddctd_cm_yo_server.exe -worldID 1
```

On this boot the framework: mounts `art.zip`, prints the **LiFx autoload banner**, extracts
`AutoloadConfig.cs` → `mods/AutoloadConfig.cs`, then loads `mods/JorvikMod` + `mods/JorvikModv2`,
registering their object-types/recipes into `sql/dump.sql`.

Wait for `Server is up and ready to accept connections`, then stop it
(`pkill -TERM -f ddctd_cm_yo_server.exe`).

### Step 5 — Enable the data export (the step the README omits)

The **released** `art.zip` ships `AutoloadConfig.cs` with `createDataXMLS = false` (even though
the repo's current copy has `true`). After the first boot, edit the extracted copy:

```
# mods/AutoloadConfig.cs
$LiFx::createDataXMLS = true; // generate recipe / recipe_requirement / objects_types xml from the DB
```

`mods/AutoloadConfig.cs` is only auto-created if missing, so this edit persists.

### Step 6 — Export boot (write the data XMLs)

```bash
ddctd_cm_yo_server.exe -worldID 1
```

With `createDataXMLS` on, `onPostInit` exports the DB tables. After ready you will have:

```
LiFx/dbexport/data/recipe.xml
LiFx/dbexport/data/recipe_requirement.xml
LiFx/dbexport/data/objects_types.xml
```

Stop the server.

> The export only contains what's in the DB. A mod object reaches the DB once its
> `sql/dump.sql` INSERT has been applied (the DB patch runs at the *start* of a boot, before the
> mods register) — so on a brand-new world the server needs **two boots**: mod objects land in
> the DB on boot 2, then their recipes FK-resolve and export.

### Step 7 — Distribute the exported XMLs

Copy the freshly-exported files into **both** the server `data/` and the client modpack base:

```bash
cp LiFx/dbexport/data/*.xml  "$SRV/data/"
cp LiFx/dbexport/data/*.xml  "$SRV/yolauncher/modpack/data/"
```

### Step 8 — Reload boot (apply the copied data)

Restart **after** copying (not before). This imports the freshly-copied `data/`, so the live
world and the data the server sends to clients now match the export.

```bash
ddctd_cm_yo_server.exe -worldID 1
```

Order: **export boot → copy → reload boot** — one start to produce the data, one to load it once
it's in `data/`.

### Step 9 — Build the client modpack

`createModpack.bat` runs `7z a modpack.zip .\yolauncher\modpack\* -r "-x!*.dso"` — i.e. zip the
**contents** of `yolauncher/modpack/` recursively, excluding `*.dso`. On Linux without 7-Zip:

```bash
cd "$SRV"
python3 - <<'PY'
import os, zipfile
base="yolauncher/modpack"
with zipfile.ZipFile("modpack.zip","w",zipfile.ZIP_DEFLATED) as z:
    for root,_,files in os.walk(base):
        for f in files:
            if f.lower().endswith(".dso"): continue
            full=os.path.join(root,f)
            z.write(full, os.path.relpath(full, base))   # zip root = contents of modpack/
PY
```

The zip's root contains `mods/`, `data/`, `Heraldry/`. Upload `modpack.zip` to
[Yo Launcher](https://www.yolauncher.app/).

---

## The data / recipe pipeline (mental model)

```
mod source INSERTs  →  DB (objects_types / recipe / recipe_requirement / skill_type)
                    →  createDataXMLS export  →  LiFx/dbexport/data/*.xml
                    →  copy to data/ + yolauncher/modpack/data/
                    →  zip yolauncher/modpack/* (minus *.dso)  =  client modpack
```

DB schema facts that drive the bugs below:

- Table is `skill_type` (**singular**).
- `recipe` foreign keys: `StartingToolsID` and `ResultObjectTypeID` → `objects_types`;
  `SkillTypeID` → `skill_type`.
- The FK `FK_recipe_starting_objects_types` rejects `StartingToolsID = 0` (there is no object id
  `0`). Recipe INSERTs use `INSERT IGNORE`, so an FK-violating row is **silently dropped**.
- The export uses `IFNULL(StartingToolsID, 0)`, so the "no tool" value in the DB is `NULL`, not
  `0`.
- Skill IDs referenced: `14` = **Healing**, `62` = **General actions** (the LiF build/decorator
  menu filters on skill `62`).
- The framework's `executeCallback` guards every mod callback with `isMethod` and **silently
  skips** a missing function — there is no cascade/error, the recipe just never runs.
- The mod's `*Requirements` callbacks **never null-check the resultSet**, so *any* failed recipe
  INSERT leaves a null resultSet and the next `%resultSet.ok()` page-faults (read `0x18`),
  crashing the engine. This makes every SQL-string bug below a hard crash, not a soft skip.

---

## Bugs found & fixed (all destined for / shipped in Jorvik1-2 v2.1.0)

All in `mods/JorvikModv2/mod.cs` unless noted.

| # | Bug | Affected objects | Fix |
|---|---|---|---|
| 1 | `HereldryFix` calls undefined `LiFx::runSql($sql)` and passes a stray 1st arg to `dbi.Update` | `heraldic_charges` table | single-arg `dbi.Update("ALTER TABLE ...")` |
| 2 | Icon `ImagePath` case mismatch `art/2D/Recipes/` + `art/2D/Items/` vs shipped lowercase | 97 Recipes + 29 Items refs | lowercase the path strings; leave `Objects/` capital |
| 3 | Horse (stand) recipe `ResultObjectTypeID=2510` (= Sow (sleep)) | `2511` unbuildable | set `ResultObjectTypeID=2511` |
| 4 | `setup()` callback name mismatches → recipe silently skipped | Aurochs Cow (sleep) `2506`; Decorator Kits `2531`-`2533` | rename registrations to match funcs |
| 5 | Coins never registered | `CopperCoins` / `GoldCoins` / `SilverCoins` | add registrations |
| 6 | `StartingToolsID=0` → FK reject → `INSERT IGNORE` drops row | Wall Torch `2500`, Small Candle `2502` | `StartingToolsID=NULL` |
| 7 | Recipe name `'Novice Decorator/'s Kit'` — the `/'` breaks the SQL string → #1064 → crash | Decorator Kits | SQL-escape as `Decorator''s Kit` |
| 8 | Recipe icons reference `Jorvik2/art/2D/items/...` files the mod never shipped | 6 coin + decorator-kit `ImagePath`s | point at vanilla `art/2D/Items/<file>.png` |
| 9 | 15 animal statues authored as taxidermy recipes — never show in the menu | see below | convert to vanilla decorator pattern |

### 1 — `JorvikMod2::HereldryFix`

Shipped broken; errored on every boot:

```cs
// BEFORE — two bugs
function JorvikMod2::HereldryFix() {
  dbi.Update(LiFxAntiCamper, "ALTER TABLE `heraldic_charges` ... ");  // stray 1st arg → runs
                                                                      //   "LiFxAntiCamper" as SQL → #1064
  LiFx::runSql($sql);                                                 // LiFx::runSql is undefined; $sql is undefined
}
```

`dbi.Update` takes a **single** SQL string (cf. `objectsConversions` in the same file). Fixed:

```cs
// AFTER
function JorvikMod2::HereldryFix() {
  dbi.Update("ALTER TABLE `heraldic_charges` COLLATE='utf8mb3_unicode_ci', CONVERT TO CHARSET utf8mb3 COLLATE 'utf8mb3_unicode_ci';");
}
```

Removes the `find function LiFx::runSql` error and the `#1064 ... near 'LiFxAntiCamper'` DB error,
and lets the `heraldic_charges` charset fix actually run.

### 2 — Icon path case

Jorvik2 recipe/item `ImagePath`s use `art/2D/Recipes/` and `art/2D/Items/` (capital) but the
shipped folders are lowercase `recipes/` / `items/`. Breaks on case-sensitive Linux clients:
recipe icons blank on the character sheet (the build-menu is fine because the `Objects/` folder
*is* capital and matches). Fix: lowercase the path strings — **97 Recipes + 29 Items refs**.
Leave `Objects/` capital.

### 3 — Horse (stand) off-by-one

Recipe had `ResultObjectTypeID=2510` (= "Sow (sleep)") instead of `2511`, leaving object `2511`
unbuildable. Isolated copy-paste off-by-one; all other animals were OK.

### 4 — `setup()` callback name mismatches

Registered names didn't match the actual functions, so the framework's `executeCallback`
(guarded by `isMethod`) silently skipped them and the recipes never ran:

- registered `AurochsCowsleep` but the func is `Aurochssleep` → **Aurochs Cow (sleep) `2506`**
- registered `NoviceDecoratorKit` / `Apprentice…` / `Master…` but the funcs are `…DecoratorsKit`
  (plural) → **Decorator Kits `2531`-`2533`**

### 5 — Coins never registered

`CopperCoins`, `GoldCoins`, `SilverCoins` were never registered at all. Added the registrations
(coins enabled per operator request).

### 6 — `StartingToolsID=0`

Wall Torch (`2500`) and Small Candle (`2502`) recipes had `StartingToolsID=0`; the FK
`FK_recipe_starting_objects_types` rejects `0` (no object id `0`), so `INSERT IGNORE` silently
dropped them. Fix: `StartingToolsID=NULL` (the "no tool" value; the export normalizes with
`IFNULL(StartingToolsID,0)`).

### 7 — Decorator-kit recipe names

Recipe names contained `'Novice Decorator/'s Kit'`; the `/'` breaks the SQL string → `#1064` →
null resultSet → the `Requirements` callback page-faulted (`%resultSet.ok()` on null → read
`0x18`), crashing the engine. Fix: SQL-escape as `Decorator''s Kit`. (Latent fragility: the
`*Requirements` callbacks never null-check the resultSet, so any failed recipe INSERT crashes the
server.)

### 8 — Missing recipe icons

Coin and decorator-kit icons referenced `Jorvik2/art/2D/items/{*_coins,decoration_kit_*}.png`,
which the mod never shipped — those icons live in the **vanilla** client at `art/2D/Items/`. Fix:
point those **6** `ImagePath`s at `art/2D/Items/<file>.png`.

### 9 — Animal statues (15) authored as taxidermy

The 15 animal statues (Horse / Aurochs Cow + Bull / Sow / Wolf / Wranen / Slave × stand / eat /
sleep) were authored as taxidermy recipes: `SkillTypeID=14 (Healing)`, `SkillLvl=90`,
`StartingToolsID=2517 (Health Book)` plus animal-part requirements, and the objects are
`IsUnmovableObject=1`. This does not fit LiF's decorator system, so they never appeared in the
build/decorator menu (which filters on skill `62` = "General actions"), and the Health Book tool
is un-equippable (`IsMovableObject=0`). Fix (operator chose): convert to the vanilla decorator
pattern — `StartingToolsID=NULL`, `SkillTypeID=62`, `SkillLvl=0`, and remove the Health Book
material requirement. After this they show and place like the vanilla trophies. **Verified
in-game 2026-06-07.**

---

## Operational gotchas

- **Process comm renames to `MainThrd`.** `pgrep -x ddctd_cm_yo_ser` misses the server — check the
  UDP port owner instead.
- **Port-zombie deadlock.** Killing a boot can leave a defunct `MainThrd` zombie holding UDP
  `28000` (its parent shell hasn't reaped it). The next boot then fails `portInit`
  (`Unable to initialize UDP - error 5 / Terminating`) and dies with a page fault. Clean
  teardown: `wineserver -k`, then confirm port `28000` is free before re-booting.
- **GM access.** Set `<adminPassword>` in `config/world_1.xml` (the test world used `JorvikGM`).
  GM item-spawn command: `/ADD type amount quality durability createDurability`.

### Benign log noise (safe to ignore)

- `Re-declaring function ... Previous declaration: .../art/main.cs` — the framework scripts are
  read once by the engine's directory loader and again by `main.cs`'s explicit `exec()`. Harmless.
- `cannot change namespace parent linkage of WeaponImage/Armor ...` — these occur in **vanilla**
  LiF too (32 on a clean boot). Not mod-related.

---

## Verification checklist

- Boot log shows the LiFx banner and **no** `runSql` / `LiFxAntiCamper` / `Unable to load shape`
  errors.
- `LiFx/dbexport/data/*.xml` exist after the export boot and contain mod objects (IDs ≥ `2400`:
  Small Wooden Shed `2403`, Flag PvP `2452`, Longhouse, …).
- DB `objects_types` / `recipe` contain the mod rows.
- `modpack.zip` has root `mods/ data/ Heraldry/`, contains the exported `data/*.xml`, and has
  **zero** `.dso` entries.
- In-game: the 15 animal statues appear in the decorator menu and place like vanilla trophies.

---

## Status & provenance

- **Runtime-verified:** the full install + export + modpack pipeline was validated end-to-end on
  a clean steamcmd install (see [`server_clean_install.md`](server_clean_install.md)). The
  animal-statue decorator conversion (bug 9) was **confirmed in-game by the operator on
  2026-06-07**. All nine fixes are shipped upstream in **Jorvik1-2 v2.1.0** (PR #7, closes #6),
  which also ships a pre-built `modpack.zip`.
- **Observed/inferred:** the FK / `INSERT IGNORE` / null-resultSet crash chain and the
  `executeCallback`/`isMethod` silent-skip behavior were diagnosed from boot logs, the `#1064`
  errors, and the page-fault (read `0x18`); the exact byte-for-byte upstream diff lives in
  `mods/JorvikModv2/mod.cs` of `LiF-x/Jorvik1-2`.
- **No engine RVAs in scope.** This is a script/SQL/data-pipeline runbook; the LiFx framework
  hooks the boot purely via TorqueScript (`art.zip` mounted at `art/`), so there is no binary
  patching. `source/server/cm_offsets.h` was cross-checked and contains no constant relevant to
  this page — every concrete number here is a DB object/skill/type id, a file/icon path, a count
  of references, an error code (`#1064`, UDP `error 5`, fault read `0x18`), or a UDP port
  (`28000`), not an image-base offset.
