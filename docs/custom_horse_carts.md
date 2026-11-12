---
title: Custom horse carts - WoodCart carrier IDs and cargo loading
author: Zbigi
assistance: ChatGPT (Codex)
status: re
domain: reverse-engineering
tags: [horse-carts, carrier, object-types, cargo, hooks]
related: [offsets.md, conventions.md, caravan_event_proposal.md]
sources: [docs/evidence/custom_horse_carts_reference.json, source/server/hooks/engine/hook_cart_carriers.cpp, source/server/hooks/engine/hook_cart_carriers.h, source/server/cm_offsets.h, source/server/cm_server.cpp, game/config/lifxpluss.xml, scripts/test_cart_carriers.cpp]
updated: 2026-11-12
---

# Custom horse carts - WoodCart carrier IDs and cargo loading

These documentation updates were generated with help from ChatGPT (Codex),
in collaboration with Zbigi, from notes and analyses collected over four months
of work on LiFXXL, a community project based on a modified LiF:YO server.

**The native repair has two parts:** map the new passive cart ObjectTypeID
to its harnessed ObjectTypeID, and admit the passive ID to the carrier cargo-load
gate. LiFXXL already contains this repair for **`3016 -> 3018`**. This contribution
ports those two boundaries to Plus as an optional `cartCarriers` hook. The
supporting object, model, datablock and ability definitions must also be present.

## Origin and the released LogCart

The starting point is [LiF-x/LogCart 1.0.0](https://github.com/LiF-x/LogCart/releases/tag/1.0.0),
commit `de6d6cf7ccce5057949a6c74e53aa47f585b3966`.
The [original mod page](https://lifxmod.com/unsupportedmods/log-cart.html) credits
Kurai for the model, Dorian for the LiFx conversion and the original mod's
repair contribution. It still lists a problem with loading logs into the cart.
This page documents the later LiFXXL carrier repair; it does not change that
release or its upstream support status.

The released files have several mismatches:

- [`Transport.cs`](https://github.com/LiF-x/LogCart/blob/1.0.0/yolauncher/modpack/mods/LiFx/LogCart/Transport.cs)
  reuses the vanilla name `HarnessedHorseCart`, sets datablock `id = 695`, and
  uses passive `objectTypeId = 3016` for the attached cart.
- [`mod.cs`](https://github.com/LiF-x/LogCart/blob/1.0.0/mods/LiFx/LogCart/mod.cs)
  registers `3016`; the registration call for a separate harnessed type is
  commented out.
- The main [`objects_types.xml`](https://github.com/LiF-x/LogCart/blob/1.0.0/data/objects_types.xml)
  has `3016`; a separate
  [older package copy](https://github.com/LiF-x/LogCart/blob/1.0.0/data/New%20folder/objects_types.xml)
  contains the intended `3018` row too. Its presence in an unused file does
  not register that type in the active server/client data.

Merge the required definitions into the active data set. Do not overwrite a
current modpack with the release's complete historical XML files or load two
versions of its registration code. This contribution does not redistribute the
models, textures or a game executable.

## Keep the identifiers separate

| Identifier | WoodCart example | Meaning |
| --- | --- | --- |
| Passive ObjectTypeID | `3016` | The cart standing in the world; native complex movable object. |
| Harnessed ObjectTypeID | `3018` | The attached cart type while harnessed to a horse. |
| Datablock name | `LifXXLHarnessedWoodCart` | A distinct `AttachedShapeData` definition; do not replace `HarnessedHorseCart`. |
| Datablock numeric ID | `693` in the checked LiFXXL data | A different ID namespace. Check availability in the destination modpack. |
| Cart instance ID | `movable_objects.ID` | One particular built cart. This is what a cargo row's `CarrierMovableID` references. |
| Cargo instance ID | Another `movable_objects.ID` | One particular log or other carried movable. |
| Runtime object ID | SimObject ID | Not an ObjectTypeID or database primary key. |

Adding a "carrier ID" to the hook means adding an **ObjectTypeID pair**. Do not
put `3016` into every cargo row's `CarrierMovableID`: that column needs the
actual cart instance's database ID. Do not replace a live object's instance ID
with its new type ID.

### Check the LiFx registry before allocating IDs

The [LiFx Object Types ID List](https://lifxmod.com/Docs/objects-types-id-list.html),
checked on `2026-11-12`, identifies `3016` as **LiFx Log Cart**, parent `77`.
Keep this existing identity when repairing that mod; it is not a free ID for
an unrelated cart. The row for `3018` is blank. Its use as the harnessed cart
is evidenced by LiFXXL and the historical package copy, **not by a confirmed
allocation in the public list**. A blank row is not proof that no mod uses it.
Coordinate the harnessed-type allocation with the LiFx registry before releasing
a generally installable content pack.

The list concerns **ObjectTypeIDs**. Its `693` entry is the plant Filia Prati;
this is a different namespace from `AttachedShapeData.id = 693`. Check numeric
datablock IDs and datablock names against the active datablock set separately.
Recipe and recipe-requirement IDs likewise have their own tables.

The hook does not register object rows, consult an online registry or migrate
IDs in a saved world. Its configuration is only a mapping over content already
installed. The disabled example preserves the known LiFXXL pair and does not
reserve it on anyone's behalf. When porting to a pack with a conflicting
allocation, agree the new mapping first and update content/references together;
do not renumber an existing world merely to make a sample match.

## Enable the Plus hook

The section in [`game/config/lifxpluss.xml`](../game/config/lifxpluss.xml) is
disabled by default. In the installed `config/lifxpluss.xml`, enable it only
after supplying the matching content:

```xml
<cartCarriers enabled="1">
    <cart passiveId="3016" harnessedId="3018" />
</cartCarriers>
```

For another cart, reserve two unused ObjectTypeIDs and append another `cart`
entry using those IDs. Keep the WoodCart entry if that mod is still installed.
The hook accepts positive decimal `uint32` IDs, rejects reused IDs across
pairs, rejects identical passive/harnessed IDs, and reserves vanilla cart IDs
`1461`, `1437`, `1497`, `1496`. Enabled empty sections and malformed entries
disable the whole section with a diagnostic; they do not install a partial list.

A missing or disabled section leaves both native sites untouched. Configuration
is read at startup; live reconfiguration is refused. The hook is registered in
`cm_server.cpp` and both the Windows and Linux build lists. It changes running
process memory through the mod DLL; it never edits an EXE or DSO file on disk.

## What the two hook boundaries do

All RVAs below belong to **`ddctd_cm_yo_server.exe` 1.4.4.5**, image base
`0x140000000`, SHA-256
`ACD99BD1A785D95B06494CBADE51A2C14EE60A521D0E88A4BE6AE1B3B685219F`.
Exact instruction bytes are in the [evidence record](evidence/custom_horse_carts_reference.json).

### 1. Passive-to-harnessed conversion

The initializer at `0x2E000` registers the stock pairs:

| Passive | Harnessed |
| ---: | ---: |
| `1461` | `1437` |
| `1497` | `1496` |

`CARRIER_CONVERT_OBJECT_TYPE = 0x23DC90` has this Win64 ABI:

```cpp
U32 __fastcall ConvertObjectType(U32 mode, U32 objectTypeId);
// ECX = mode; EDX = input ObjectTypeID; EAX = result.
```

At `0x3D9DCA` the harness path sets `ECX=1`, then calls it at `0x3D9DCF`.
The [Plus handler](../source/server/hooks/engine/hook_cart_carriers.cpp) checks
the configured pairs only for mode `1`; a matching passive ID returns its
harnessed ID. All other calls go to `_Carrier_ConvertObjectType` unchanged.

The original function returns the input for mode `0`, and for an unmapped ID
in mode `1`. Other modes use a diagnostic path and return zero. Thus "all other
modes return the input" would be inaccurate. There is no invented reverse
`3018 -> 3016` rule in this detour; native unharnessing retains its own lifecycle.

### 2. Recognize the passive cart when loading its cargo

The function beginning at `0x24C290` checks the complex object's type:

```text
0x24C3C4  cmp eax,1461
0x24C3C9  je  0x24C3D6
0x24C3CB  cmp eax,1497
0x24C3D0  jne 0x24C474
```

The accepted path constructs an Entity and calls the cargo-list loader
`0x24F100` at `0x24C430`. A custom `3016` otherwise skips this path, even if
its XML and harness mapping exist. This is why the carrier recognition hook
matters to the log-loading repair.

The Plus trampoline admits each configured **passive** ID and the two original
passive types. It resumes at `CARRIER_PASSIVE_ACCEPTED = 0x24C3D6` or
`CARRIER_PASSIVE_REJECTED = 0x24C474`. It preserves registers, stack and the
original comparison flags for ordinary/rejected objects. The two target byte
patterns are checked before installation. A signature mismatch installs neither
part; a Detours failure restores the original gate.

This is an 18-byte **mid-function** gate, not a function entry with a C++ calling
convention. Do not attach a normal function detour there or globally spoof the
shared ObjectTypeID getter at `0xD3790`. Other systems need the real type.

## Required content on server and client

The complete LiFXXL definitions are summarized in the evidence record. The
following is a porting checklist, not an automatic content installer:

1. **Object rows:** define both `3016` and `3018`, with `ParentID=77`,
   `IsContainer=1`, `IsMovableObject=1`, `IsUnmovableobject=0`. The checked rows
   use `Length=7`, `UnitWeight=10000`, `MaxContSize=1000000`. Preserve normal
   object fields and choose balance values deliberately for a new cart.
2. **Passive shape:** the `cm_objects.xml` entry for `3016` uses
   `art/addons/woodCart/LogCart_passive.dts` in its Complete state.
3. **Attached shape:** `3018` uses `art/addons/woodCart/LogCart.dts`. Keep the
   required state definitions, model nodes, materials and icons consistent.
   Both peers need the same content; a server-side type registration alone
   cannot supply a missing client model.
4. **Datablock:** register a unique attached shape, with its `objectTypeId`
   pointing at the harnessed row. The example below is the checked LiFXXL
   allocation; `693` is not promised free in another pack.
5. **Abilities:** extend the existing entity lists below while retaining the
   original state, skill, distance and access requirements. Supply the resulting
   ability XML on both peers.
6. **Recipe:** create the passive type. The checked recipe `1093` produces
   `3016`, uses StartingToolsID `36`, skill `8`, level `90`; its four materials
   are `235 x30`, `255 x4`, `1131 x20`, `262 x10`. Reserve independent recipe
   and requirement IDs for an additional cart. `1093` is existing WoodCart data.

```tcl
datablock AttachedShapeData(LifXXLHarnessedWoodCart)
{
    id = 693; // This modpack's allocation; check before reusing.
    objectTypeId = 3018;
    shapeFile = "art/addons/woodCart/LogCart.dts";
};
```

The content loader must actually execute that definition during datablock
loading on both peers. Copying a `.cs` into an arbitrary folder is insufficient.
Client material definitions must also be loaded. Passenger seating requires
appropriate model nodes and its own implementation; a carrier pair does not add
passenger seats.

| Skill | Ability | Entity entries added by WoodCart |
| ---: | --- | --- |
| `62` | `108` Look in Inventory | `attached_obj`: `3018` |
| `62` | `288` Harness | `complex_obj`: `3016` |
| `62` | `289` Unharness | `attached_obj`: `3018` |
| `62` | `300` Put in Cart | `complex_obj`: `3016`; `attached_obj`: `3018` |
| `62` | `301` Pull from Cart | `complex_obj`: `3016`; `attached_obj`: `3018` |
| `63` | `224` Get On | `attached_obj`: `3018`; seating behavior is a separate concern |
| `65` | `312` Rename | `complex_obj`: `3016` |

For a fresh type, substitute both IDs consistently in the object rows, models,
datablock, relevant ability entries, recipe and `cartCarriers` pair. Preserve
existing cargo/container records when updating an installed cart.

## Inventory capacity and carried logs are separate

`MaxContSize` controls ordinary inventory capacity. The checked source and
generated client rows for both WoodCart types contain `1000000` (displayed as
`1000` under the normal `/1000` inventory scale). Earlier local notes contain
older capacities; those are not the current checked rows.

Whole logs use the **carried movable-object list**. At `0x37E31B..0x37E33E`,
the native Put in Cart path requires a nonzero size byte at
`objectType->properties + 0x1C` and checks `used + size <= 12`.
`0x24ED60` supplies the occupied-size sum. Thus this budget measures summed
object sizes, not a fixed number of log records.

LiFXXL separately extends WoodCart's budget to `30`, for both `3016` and
`3018`, and updates its displayed counter. For logs of size `2`, that means
`15` logs, with the next one rejected. **The `cartCarriers` hook does not
change this limit.** Plus has a separate [cartPlaces proposal, PR #8](https://github.com/LiF-x/Plus/pull/8);
its acceptance and availability must be checked independently.

When implementing a type-specific capacity extension, distinguish
`ComplexObject_Entity` (kind `3`, object type through `object+0x370`, ID at
`type+0x60`) from `AttachedObject_Entity` (kind `4`, resolver `0x33DDA0`,
attached datablock at `+0x9E8`, type ID at datablock `+0x2E8`). Checking only
the passive type produces different behavior after harnessing. Keep native
sum-of-sizes accounting, admission and the displayed counter consistent.

## Save, load and diagnosing missing cargo

The standard database relationship is:

```text
cart instance: movable_objects.ID
    <- cargo row: movable_objects.CarrierMovableID
```

`CarrierMovableID` is nullable and has a foreign key to `movable_objects.ID`.
The hook uses this native relationship; it introduces no cart storage table or
replacement log item. `CarrierHorseID` and `CarrierCharacterID` describe other
carrying states and must remain under the normal engine lifecycle.

For read-only diagnosis, bind `:cart_db_id` to the cart instance ID in your SQL
client. These are parameterized examples, not commands executed by this PR:

```sql
SELECT ID, ObjectTypeID, CarrierHorseID, CarrierCharacterID, CarrierMovableID
FROM movable_objects WHERE ID = :cart_db_id;

SELECT ID, ObjectTypeID, CarrierMovableID
FROM movable_objects WHERE CarrierMovableID = :cart_db_id;
```

| Symptom | Inspect first |
| --- | --- |
| No Harness option | Ability `288` entity list, completed state, ordinary prerequisites. |
| Harness fails or wrong model appears | Passive/harnessed mapping, existence of `3018`, unique datablock name/ID, model loading. |
| Cargo missing from the restored list | Passive gate, saved `CarrierMovableID`, loader path; preserve the original records. |
| Ordinary inventory works but whole logs fail | Put/Pull entity entries, cargo registry, nonzero object size and summed size budget. |
| Capacity differs when harnessed | Both Entity representations and both type IDs in the capacity extension. |
| Cart disappears on emergency unharness or horse death | Native placement/drop result and original cart record; this is a separate failure path, not proven fixed by adding a carrier ID. |

Do not repair an unexplained missing list by spawning replacement logs, clearing
carrier references or resetting containers. First establish whether the existing
records remain and whether the runtime cargo list was reconstructed.

## Status & provenance

- **Existing server context:** LiFXXL is a running community server with players;
  its maintainer reports making WoodCart operational. The source hook and addon
  definitions above are present in that project. This is not an assertion that
  every historical disappearance problem is solved.
- **Checked for this contribution:** release `1.0.0` files, nine native byte
  ranges, current addon rows and their generated client counterparts. Public
  evidence contains selected data and source excerpts, without player records.
- **Plus port:** optional carrier hook, lifecycle/configuration wiring and both
  build lists are included here. Its regression harness executes the generated
  machine code and real Detours against a synthetic image: `65,651` checks pass,
  including all `65,536` low IDs, flags, mode forwarding, signature rejection,
  transaction conflict and restoration on detach. Modified C++ units compile
  with MSVC. These checks do not constitute a new in-game acceptance session.
- **Full build limitation:** the repository-wide MSVC build stops at the
  pre-existing zero-length `kExtensionRows` array in
  `source/server/hooks/furnace/hook_proc_desc.cpp:107` (`C2466`). The carrier
  translation unit and its executable test compile successfully; this PR does
  not establish a successful full DLL build or deployment.
- **Gameplay acceptance after installation:** with an ordinary player, build the
  cart, open its inventory, put/pull whole logs, harness, drive, put/pull while
  harnessed, unharness, reconnect, and verify persistence after a planned restart.
  Check original cargo IDs, quality and quantities, both vanilla cart pairs,
  and the configured capacity boundary. Models, wheel animation, collisions,
  placement and horse-death behavior require their own observations.

The carrier implementation and data are not deployed to any running server by
this contribution. Build instructions and hook conventions are in
[Conventions](conventions.md#recipe-for-adding-a-new-hook); this carrier mechanism
is separate from the [AI caravan proposal](caravan_event_proposal.md).
