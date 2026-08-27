---
title: Bloomery & furnace/recipe system
status: verified
domain: reverse-engineering
tags: [bloomery, furnace, recipes, crafting]
related: [ai_and_spawning.md, reverse_engineering.md]
updated: 2026-06-26
---

# Bloomery and the Furnace / Recipe System

How the in-game bloomery actually works under the hood, and every function worth hooking to either log or override its material-transformation logic.

## TL;DR

- The bloomery is **not** its own C++ class. It's a *runtime instance* of **`WorkingFurnace : AbstractCraftworkFurnace`**, distinguished from the kiln only by the object-type loaded from the DB and the recipes attached to that type.
- **The "hidden transformation" you noticed lives in `WorkingFurnace::Recycle` @ RVA `0x1DB840`.** It's recursive over a "child-material tree" — that's why a single furnace operation can turn iron ore into iron blooms in one step even though no TorqueScript function is exposed for the conversion. The error strings `"Recursion fail on input type %u"` and `"Can't find child material type %u"` are baked into this function.
- For a logger / "expose smelting outputs" mod, hook `WorkingFurnace::Recycle` (per-operation) or `AbstractCraftworkFurnace::Craft` @ `0x1D4390` (per-craft). Both have full context (player, input materials, recipe IDs).
- The recipe data itself is in **`CmRecipesManager`** (singleton) — to enumerate all bloomery recipes from C++, look it up there. Recipe lookup helper: **`CmCreationManager::findRecipe`** @ `0x1E6800`.

## Class hierarchy (recovered from RTTI + strings)

```
ConsoleObject  ← Torque root
   └── …  ← intermediate Torque/cm_yo classes (vtable slots 1, 2, 4, 10 = inherited)
         └── AbstractCraftworkFurnace          // abstract base
                ├── WorkingFurnace             // bloomery, kiln, smelter
                └── BrewingTankFurnace         // brewing tank / fermenter
```

Empirical evidence: vtables of `WorkingFurnace` and `BrewingTankFurnace` share their slots 1, 2, 4, 6, 7, 10 with `AbstractCraftworkFurnace` — confirming inheritance — and override slots 0, 3, 8, 9, 11, 12 with class-specific code. The base class itself has `_purecall` in slots 8, 9, 11 — those are the pure-virtual customization points each derived class **must** implement.

The bloomery's "ability" verb in the UI is `AbilityImp::ManageSmelting` (the kiln uses `AbilityImp::ManageKiln`). The bellows interaction is `AbilityImp::UseKilnBellows` / `UseBellows`. None of these expose TorqueScript functions; everything is C++.

## Vtable summary (the comparison that proves inheritance)

| Slot | `AbstractCraftworkFurnace` | `WorkingFurnace` | `BrewingTankFurnace` | Meaning |
|------|---------------------------|------------------|----------------------|---------|
| 0 | `0x1D4310` | `0x1D75B0` | `0x1DAAA0` | scalar-deleting dtor (per class) |
| 1 | `0x1DF800` | inherited | inherited | base inherited virtual |
| 2 | `0x1DF860` | inherited | inherited | base inherited virtual |
| 3 | `0x8A060` *(stub)* | **`0x1DCFF0`** (`WorkingFurnace::recalcTick`) | `0x1DB370` | per-class **tick callback** |
| 4 | `0x1DF5E0` | inherited | inherited | base inherited virtual |
| 5 | CFG stub | CFG stub | CFG stub | unused |
| **6** | **`0x1D4390`** (`AbstractCraftworkFurnace::Craft`) | inherited | inherited | **the central craft routine** |
| **7** | **`0x1D5B70`** (`AbstractCraftworkFurnace::consumeWoodForFuel`) | inherited | inherited | **fuel consumption** |
| 8 | `_purecall` | `0x1DCFB0` | `0x1DB340` | pure-virtual (derived must implement) |
| 9 | `_purecall` | `0x1D9700` | `0x186490` | pure-virtual |
| 10 | `0x1D5F40` | inherited | inherited | base inherited virtual |
| 11 | `_purecall` | `0x1DC3F0` | `0x1DABE0` | pure-virtual |
| 12 | `0x9DE40` | inherited | `0x1DAC10` | base/BrewingTank-override |

## Hookable functions (with RVAs and source-string evidence)

### Furnace lifecycle / operation

| Symbol | RVA | Type | What you can do by hooking here |
|---|---|---|---|
| **`AbstractCraftworkFurnace::Craft`** | **`0x1D4390`** | virtual slot 6 (server-side action) | Intercept *every* craft attempt on *any* furnace (bloomery, brewing tank, etc.) — see the recipe ID, the inputs, the player. Error strings emitted here: `Invalid skill amount`, `Can't consume tool durability`, `Can not consume device durability`, `Items quantity is not enough for the recipe`, `Can't create object`. |
| `AbstractCraftworkFurnace::Craft` *(callback)* | `0x1D4150` | regular function | Async completion callback for `Craft`. Error strings: `Can't find object inside callback`, `Can't find charID/player inside callback`, `Can't lift object`. |
| **`AbstractCraftworkFurnace::consumeWoodForFuel`** | **`0x1D5B70`** | virtual slot 7 | Tax / multiplier hook for fuel consumption. Error: `trying to divide by 0, because item weight is wrong`. |
| **`WorkingFurnace::recalcTick`** | **`0x1DCFF0`** | virtual slot 3 | Periodic tick while a furnace is "working" — runs on every server tick a bloomery is heating. Error: `tick is longer than 1.0!`. This is where progress accumulates. |
| **`WorkingFurnace::Recycle`** | **`0x1DB840`** | **non-virtual** member function | **The hidden material-transformation engine.** Recursive over the child-material tree — iron ore → bloom → ingot transitions, etc. Error strings emitted here: `Bad player`, `skill id=%u not found`, `Can't find material type %u`, `Can't find child material type %u`, `Recursion fail on input type %u`. |
| `WorkingFurnace::_getMinChildTreeUnitWeightKg` | `0x1DC280` | regular function | Walks the same child-material tree to compute minimum unit weight. A read-only oracle for the transformation graph — useful for *introspection* without altering anything. |
| `WorkingFurnace` slot 8 (per-class virtual) | `0x1DCFB0` | virtual | Unknown semantics — adjacent to `recalcTick`; likely the "is working / should tick" predicate. Worth decompiling. |
| `WorkingFurnace` slot 9 | `0x1D9700` | virtual | Unknown — likely the "produce output object" routine (smelter's discrete moment of materialization). |
| `WorkingFurnace` slot 11 | `0x1DC3F0` | virtual | Unknown — adjacent to `_getMinChildTreeUnitWeightKg`; likely a related recipe-tree walker. |

### Player-facing abilities (UI verbs)

These are the *server-side handlers* for what the player clicks in the radial menu when targeting a bloomery:

| Symbol | RVA | What it does |
|---|---|---|
| `AbilityImp::ManageSmelting::_onDoPerform` | `0x3A2820` | The "Manage Smelting" verb on a bloomery. Error strings: `Can't find game connection`, `Bad entity`, `Can't find entity object`, `Bad object inventory`, `can't select object inventory`. Hook here to gate access (e.g., guild permissions). |
| `AbilityImp::ManageKiln::_onDoPerform` | *(grep ManageKiln in strings.tsv if needed)* | "Manage Kiln" verb on a kiln. |
| `AbilityImp::UseKilnBellows::_onDoPerform` | `0x3A42C0` | "Use Bellows" on a kiln. |
| `AbilityImp::UseBellows::_onDoPerform` | `0x3A40D0` | "Use Bellows" generic (likely bloomery & smelter). |

### Recipe data layer

| Symbol | RVA | What it does |
|---|---|---|
| `CmRecipesManager` *(singleton)* | vftable `0x7EEC58` | Holds every recipe loaded from the DB. Almost every furnace function queries it. Singleton accessor: `SingletonCreationInfo::Singleton<CmRecipesManager>::instance`. |
| `CmCreationManager::findRecipe` | `0x1E6800` | The recipe lookup function. Error strings: `Can't find recipe for blueprintID=%u`, `Can't find recipe for recipeID=%u`, `User trying to exploit recipeID=%u`. The third one is the server-side exploit check — interesting hook target for cheat detection. |
| `BluePrintsManager::_loadRecipePossibleBlueprints` | `0x3063D0` | Loads the recipe → blueprint mapping. Query string: `SELECT 'ID', 'RecipeID', 'BaseRecipeID' FROM 'recipe_possible_blueprints';` |
| `BluePrintsManager::getBlueprintRecipeID` | `0x307370`, `0x3086B0` | Resolves a blueprint to its recipe. |
| `BrewingTankFurnace::makeRecipesList` | `0x1DAC50` | Enumerates the recipes valid for a brewing tank — analogous function presumably exists for `WorkingFurnace` (decompile in Ghidra to confirm). |

## What "hidden materials" actually means

The TorqueScript layer can ask "what's the recipe for object type X?" but the LiF C++ code keeps the **conditional, recursive** branches private. Concretely:

- The recipe rows in the DB (`objects_types` table joined to `recipe`) describe single transformations.
- `WorkingFurnace::Recycle` walks a **tree** of recipes — if input type T has a "child" recipe, the function recurses into the child first. This is how a single "smelt" pass can turn raw ore → bloom → ingot, with intermediate types that aren't ever materialized as inventory items.
- The TorqueScript binding for recipes was never wired up for these intermediates, so a player UI can't preview them.

To expose this from a mod, your options are:

1. **Hook `Recycle` and log every (player, input_type, child_type, output_type, recipe_id) tuple.** Trivial — just log inside the hook. Produces an experimental log of the actual transformations as they happen.
2. **Hook `_getMinChildTreeUnitWeightKg` and walk the tree from C++.** Read-only introspection — no behavioral change.
3. **Add a Con::AddCommand "smeltGraph"-style script function** that asks `CmRecipesManager` to enumerate all recipes whose output type is consumed by another recipe. This is the cleanest static analysis but requires understanding `CmRecipesManager`'s data layout (decompile its singleton instance plus a few read methods).

Option 1 is the smallest change and produces the most useful evidence.

## Suggested first-mod hook for the bloomery

To log every smelting transformation that happens on the server:

```cpp
// hooks/gameplay/hook_furnace.h
__CM_DECL_INTERNAL(void, __fastcall, _WorkingFurnace_Recycle,
                   void* self, void* player, unsigned int materialTypeId);
// Note: this signature is a guess from the error-string arguments
// ("Bad player", "Can't find material type %u"). Confirm by decompiling
// FUN_1401db840 in Ghidra — the first three RCX/RDX/R8 registers will
// show you the actual types.

namespace Hooks::Gameplay {
    void WorkingFurnace_Recycle(void* self, void* player, unsigned int matId);
}

// hooks/gameplay/hook_furnace.cpp
__CM_INSTATNTIATE(_WorkingFurnace_Recycle);

void Hooks::Gameplay::WorkingFurnace_Recycle(void* self, void* player, unsigned int matId) {
    Con::Echo("[furnace] Recycle  furnace=%p player=%p input_material_type=%u", self, player, matId);
    _WorkingFurnace_Recycle(self, player, matId);
}
```

```cpp
// cm_offsets.h
WORKING_FURNACE_RECYCLE = 0x1DB840,

// cm_server.cpp -> Lifx::Server::AttachHooks()
__CM_ATTACH_HOOK(CmOffset::WORKING_FURNACE_RECYCLE,
                 _WorkingFurnace_Recycle,
                 Hooks::Gameplay::WorkingFurnace_Recycle);
```

After deploying, every bloomery/kiln operation will produce a `[LiFx] [furnace] Recycle furnace=… player=… input_material_type=…` line in the console. Then decompile FUN_1401db840 to learn how it picks the output material — and iterate from there.

## The "whitelist" — why a new ore vanishes without producing output

**Symptom:** add a new `objects_types` row for `tin_ore` plus a `recipe` row mapping it to a new `tin_ingot`, put the ore into a bloomery, let the work period finish — the input gets consumed but no output spawns. No error message in the console.

**Root cause:** the bloomery's per-tick processing routine `WorkingFurnace::recalcTick` (`0x1DCFF0`) iterates items in the furnace and filters each one through `Type::hasParent(targetTypeId, maxDepth)` (`0x27EB30`):

```c
if (Type::hasParent(item->type, 0xCE, 100) ||
    Type::hasParent(item->type, 0xD5, 100)) {
    // …process this item, advance smelting timer, eventually spawn output
} else {
    // silently skip — input is still consumed elsewhere in the loop,
    // but no smelting state advances and no output is queued
}
```

`0xCE` = type ID **206** and `0xD5` = type ID **213**. These are two abstract parent type IDs in `objects_types` — most likely the LiF "smeltable iron-class" and "smeltable non-iron / metal" parents (verify in your DB).

For a new ore to be recognized, its `ParentID` chain must climb up to **one of those two type IDs within 100 hops**. The `recipe` table being correct is necessary but not sufficient.

### `Type::hasParent` decompiled

```c
// FUN_14027eb30 — Type::hasParent(this, target, maxDepth)
int hasParent(Type* self, int targetId, int maxDepth) {
    if (self->id == targetId) return 1;
    int parentId = self->parentId;              // field at offset +0x18
    int depth = 0;
    while (parentId != 0 && depth < maxDepth) {
        if (parentId == targetId) return 1;
        Type* p = getTypeById(globalMgr, parentId);   // FUN_14027ca00
        if (!p) break;
        parentId = p->parentId;
        depth++;
    }
    return 0;
}
```

#### What does the `100` mean?

It's the **recursion depth cap**, not an ID range. Each iteration of the loop follows exactly one `ParentID` link and bumps `depth`; the loop bails when `depth == maxDepth`. So `hasParent(t, 0xCE, 100)` reads as *"is type 0xCE reachable from `t` within 100 ancestor hops?"* Type IDs are full 32-bit integers — they never enter the comparison with this constant.

The cap is defensive. `_validateAllTypes` already rejects cyclic parent chains at server startup, but the runtime walker bounds itself anyway in case the in-memory tree ever gets corrupted. In practice the real LiF hierarchy is 5–8 levels deep — `100` is just a generous sentinel that no honest data will ever approach.

Practical implication: when you build your own hierarchy (e.g., a `Tin metals → Metallic processed materials` chain), you can nest as many intermediate abstract parents as you want without worrying about this number. The cycle check from `_validateAllTypes` is the real constraint; this depth cap is a belt-and-braces backup.

### The `objects_types` shape rules (from `CmInventoryBase::_validateAllTypes` `0x2797C0`)

| Rule | Violation error string |
|---|---|
| Every `ParentID` in the chain must resolve to an existing `objects_types` row | `validation of ObjectTypeID=%u failed: one of parent types (%u) not found` |
| No cycles in the parent chain | `validation of ObjectTypeID=%u failed: recursive parent chain` |
| Abstract parent types (only used to organize the hierarchy) must have `UnitWeight = 0` | `ObjectTypeID=%u is parent type and has non-zero weight` |
| Concrete (instantiable) items must have `UnitWeight > 0` | `ObjectTypeID=%u has zero weight` |

If any of these fire at server startup, your new type is excluded entirely — it won't even be loadable.

### How to fix or bypass

**Option 1 — DB-only fix (preferred, no code change).** Make the new `tin_ore` row descend from an existing parent in the smeltable hierarchy. First identify the magic parents in your DB:

```sql
SELECT ID, Name, ParentID, UnitWeight
FROM objects_types
WHERE ID IN (206, 213);
```

Then either:

- Set `tin_ore.ParentID` directly to 206 (or 213), if the existing semantics fit.
- Or, more typically: add an intermediate abstract type `tin_ore_class` with `ParentID = 206` (or `213`) and `UnitWeight = 0`, then `tin_ore.ParentID = tin_ore_class.ID`. Same for the corresponding `tin_ingot` if it also needs hierarchy membership.

Check both items pass `_validateAllTypes` after the change (no orphan parents, no cycles, parents = weight 0, concrete = weight > 0).

**Option 2 — LiFx hook to extend the whitelist.** Hook `Type::hasParent` at `0x27EB30`. In the hook, maintain an `unordered_set<uint32_t>` of "extra allowed type IDs" loaded from `lifxpluss.xml`. When the engine asks `hasParent(myType, 0xCE, 100)` or `hasParent(myType, 0xD5, 100)`, if `myType->id` is in your set, return 1 unconditionally; otherwise tail-call the original. ~25 lines of C++. Cleanest mod-side approach because it changes nothing about the DB schema.

Pseudocode:

```cpp
// cm_offsets.h
TYPE_HASPARENT = 0x27EB30,

__CM_DECL_INTERNAL(int, __fastcall, _Type_hasParent,
                   void* self, int targetId, int maxDepth);

int Hooks::Gameplay::Type_hasParent(void* self, int targetId, int maxDepth) {
    // self->id at offset +8 (confirmed in _getMinChildTreeUnitWeightKg decompile)
    int selfId = *((int*)((char*)self + 8));
    if ((targetId == 0xCE || targetId == 0xD5) && Lifx::Bloomery::allowed(selfId))
        return 1;
    return _Type_hasParent(self, targetId, maxDepth);
}
```

**Option 3 — LiFx hook to replace `recalcTick`.** Heavier — own the entire per-tick loop. Use only if you need behavior the existing loop can't express (e.g., different output stochastics, multi-output recipes, anti-cheat per item).

### Investigation hook for "what's actually being rejected"

If the symptom isn't quite the above and you want to *see* the filter in action, hook `Type::hasParent` and log every call where the answer is false for a furnace context:

```cpp
int Hooks::Gameplay::Type_hasParent_logged(void* self, int targetId, int maxDepth) {
    int selfId = *((int*)((char*)self + 8));
    int result = _Type_hasParent(self, targetId, maxDepth);
    if (!result && (targetId == 0xCE || targetId == 0xD5))
        Con::Echo("[bloomery] reject type=%d (no ancestor %d within %d)",
                  selfId, targetId, maxDepth);
    return result;
}
```

This will print every rejection so you know exactly which type IDs are being filtered and against which target — invaluable for finding any *other* parent IDs the bloomery accepts that we haven't identified.

## `recalcTick` — full walkthrough

`WorkingFurnace::recalcTick` at RVA `0x1DCFF0` is the per-frame heartbeat for every working furnace instance (bloomery, kiln, smelter, brewing tank — they all share this implementation). It's invoked with `(furnaceThis*, dt, finalizeFlag)` and decides, for each item currently inside the furnace, whether to advance its progress quality and by how much.

The whole behavior is driven by a single in-memory table — `DAT_140acfa60` — that maps "input type hierarchy" → "processing rules". Walking the function in detail:

### 1. Furnace state setup (the prologue)

```c
if (param_1[5] == 0) return 0;           // furnace not active
if (dt > 1.0f) dt = 1.0f;                // clamp & warn ("tick is longer than 1.0!")
uVar21 = (dt * DAT_14073AB78)            // dt scaled to "progress units per tick"
                                          // DAT_14073AB78 is a global tick-rate constant
FUN_1401df4b0(furnace, &contents);       // fetch the container view of furnace contents
```

The "furnace state" field is `param_1[5]` (offset `+0x28` on the furnace). Two values matter in the body:

- **`2000`** = **HOT** (actively heated; bellows or fuel keeping it lit).
- **`500`** = **COOL** (banked / cooling down).

These are the two states the multi-stage thermal cycle (vostaskus) switches between.

### 2. Furnace kind tag (`iVar7`)

```c
uVar12 = FUN_1400c9930(furnaceContents);   // get furnace's object_type ID
iVar7 = FUN_1400d3790(uVar12);             // read type_info+0x60: the "kind tag"
```

`FUN_1400d3790` is literally `return *(uint*)(typeInfo + 0x60);` — there's a per-type "kind" field on every object-type record. Values that appear in `recalcTick`:

| `iVar7` value (hex / dec) | What it represents |
|---|---|
| `0x75` = 117 | Furnace kind that uses path B (the "type-324-iron-ore-special-case" handler) |
| `0x6b` = 107 | Uses tick-rate group A (1500/1000/500 thresholds, divisor 15→10→5) |
| `0x8a` = 138 | Uses tick-rate group A |
| `0x1c5` = 453 | Uses tick-rate group B (1500/1000/500 thresholds, divisor 10→5→5) |
| `0x88` = 136 | Uses tick-rate group B |

Other furnace kinds fall through to no special tick-rate adjustment (multiplier = 0). Identify your kiln's kind tag by `SELECT * FROM objects_types WHERE Name LIKE '%kiln%'` and look at the resulting type-info `+0x60`.

### 3. The inventory loop

For each item slot in the furnace's contents:

```c
lVar4  = inventory_slot
lVar14 = *(longlong*)(lVar4 + 0x18);      // item data ptr
lVar13 = FUN_1401db7c0(lVar14);           // look up the item's process-descriptor row
```

`FUN_1401db7c0(itemTypeInfo)` is the **central dispatch lookup**. It walks the table `DAT_140acfa60` and returns the first row whose `typeId` field is an ancestor (via `hasParent(itemType, row.typeId, 100)`) of the item's own type. The returned `lVar13` is a 28-byte row from the table.

The whole rest of the function is a 3-way branch on whether a descriptor was found and what's in it.

### 4. The three execution paths

```c
if (lVar13 == 0 || *(char*)(lVar13 + 0x10) != 0) {
    // PATH A — recipe-driven smelting (bloomery default)
}
else if (iVar7 == 0x75) {
    // PATH B — furnace kind 0x75 special case
}
else {
    // PATH C — generic baking / thermal / multi-stage
}
```

The `+0x10` byte in a table row is a flag: when **non-zero**, it forces this type into Path A regardless of any other settings. Path A is also taken when there's *no* row for this item (then the item must be matched to a recipe directly).

#### Path A — recipe-driven (bloomery)

```c
recipe = CmRecipesManager.getRecipeForType(input.typeId);
if (recipe == nullptr) {
    abort_item(slot);                     // FUN_14029daa0 — sets the slot's "op code" to 0
} else {
    for (each recipe iteration) {
        output_type = getTypeById(iteration.outputTypeId);
        if (hasParent(output_type, 0xCE, 100)        // "Metallic processed materials" (206)
         || hasParent(output_type, 0xD5, 100)) {     // "Metallic manufactured products" (213)
            advance_progress(slot, dt_scaled);
            if (furnace.state == 2000 && slot.quality < 100 && finalize)
                set_quality(slot, 100);
            goto next_item;
        }
    }
    abort_item(slot);                     // none matched
}
```

This is the bloomery's filter we already documented. The output type must descend from 206 or 213; otherwise input is silently aborted.

#### Path B — furnace kind 0x75, with `iVar8 = row[+4]`

```c
iVar8 = lVar13[+4];
if (iVar8 == 7 || iVar8 == 8) {           // baking-style accepted in kind 0x75
    if (slot.quality < row.+0x18)
        advance_progress(slot, dt_scaled * 2);   // DOUBLE rate
    if (finalize) set_quality(slot, 100);
}
else if (iVar8 == 1) {
    if (input.typeId == 0x144)            // **only iron ore (324)**
        same as iVar8 == 7/8 above;
    // any other input type with iVar8==1 here: silently skipped
}
else if (iVar8 == 2) {
    set_quality(slot, 100);
}
// other iVar8 values: fall through; item sits unprogressed
```

The "input type id == 324 special case" is the smoking gun that path B is the **iron-ore + ore-flux** hybrid kind — probably the actual bloomery.

#### Path C — generic (kiln, brewing tank, smelter, …)

```c
iVar8 = lVar13[+4];
if (iVar8 == 1) {
    // skip — handled by path A or specific path-B special case
    goto next_item;
}
if (iVar8 == 4 || iVar8 == 6) {
    if (slot.quality < row.+0x18)
        advance_progress(slot, dt_scaled);
    if (finalize) set_quality(slot, 100);
}
else if (iVar8 == 5) {
    // ===== VOSTASKUS / DAMASCUS multi-stage thermal cycle =====
    if (dt_scaled == 10) {                            // exact tick-count gate
        if (furnace.state == 2000) {                  // hot phase
            if (slot.quality <  20)              set_quality(slot,  20);   // stage 1 heat
            else if (slot.quality - 40 < 20)     set_quality(slot,  60);   // stage 3 heat
            else if (slot.quality - 80 < 20)     set_quality(slot, 100);   // stage 5 heat (DONE)
        }
        else if (furnace.state == 500) {              // cool phase
            if (slot.quality - 20 < 20)          set_quality(slot,  40);   // stage 2 cool
            else if (slot.quality - 60 < 20)     set_quality(slot,  80);   // stage 4 cool
        }
    }
}
else if (iVar8 == 2 || iVar8 == 3) {
    set_quality(slot, 100);                           // single-step instant
}
// iVar8 ∈ {7, 8}: fall through — these are path-B only
```

### 5. Tick-rate post-processing

After the inventory loop, `recalcTick` advances the furnace's *own* temperature/heat counter:

```c
uVar9 = (**(code **)(*param_1 + 0x40))(param_1);      // virtual: tick rate hint (slot 8)
uVar11 = furnace.temperature;                         // +0x2C
if (uVar11 < uVar9) (**(code **)(*param_1 + 0x50))(param_1);   // overflow handler

// Choose a "kindling-burn divisor" based on the kind tag:
if (iVar7 == 0x6b || iVar7 == 0x8a) {
    // group A: kindling burn = 15 if temp<500, else 10 if temp<1000, else 5
}
else if (iVar7 == 0x1c5 || iVar7 == 0x88 || iVar7 == 0x75) {
    // group B: kindling burn = 10 if temp<500, else 5
}
else {
    // no kindling burn for other kinds
}
```

These tick-rate groups are what makes some furnaces consume fuel faster than others.

## The process-descriptor table — `DAT_140acfa60`

Every furnace-recipe behavior is described by one row in this 59-entry table. Each row is 28 bytes (7 × `uint32`). Full dump below (run `scripts/dump_furnace_table.py` to reproduce).

Row layout:

| Offset | Field | Meaning |
|---|---|---|
| +0x00 | `typeId` | Apply this row to any object whose type-hierarchy descends from this type ID (via `hasParent`). |
| +0x04 | `kind` | Discriminator selecting which branch of `recalcTick` handles this item: 1, 2, 3, 4, 5, 6, 7, 8 — see §4 above. |
| +0x08 | `factor` (`float`) | Always `1.0f` (`0x3F800000`) for kind 5/6 rows. Probably a quality multiplier; not directly read in `recalcTick`. |
| +0x0C | `outputTypeId` | The type the input transforms *into* when finalized (kind 5/6 rows only). |
| +0x10 | `flag` (byte) | **If non-zero, forces this item to path A (recipe-driven) regardless of `kind`.** Used to mix in recipe-driven items with table-defined ones. |
| +0x14 | `field5` | Returned by `WorkingFurnace::slot 11` when `kind==1`. Function unclear; values: `1`, `4`, `5`, `40`. |
| +0x18 | `tempThreshold` | Minimum furnace temperature/state needed to advance this item. `500`, `1000`, `1500`, `2000`. |

The 59 rows (verified contents of the current Steam build):

```
 #  +0(typeId)  +4(kind)   +8(factor)   +0xC(out)  +0x10(flag)  +0x14  +0x18(temp)
 0     324         1        0           0            0           5     500
 1     325         1        0           0            0           4       0
 2     326         1        0           0            0           5       0
 3     327         1        0           0            0           4       0
 4     256         1        0           0            0          40       0
 5     236         1        0           0            0           1       0
 6     237         1        0           0            0           1       0
 7     233         1        0           0            0           4       0
 8     643         1        0           0            0           1       0
 9     328         4        0           0            0           0    1500
10     238         4        0           0            0           0    1000
11     239         4        0           0            0           0    1000
12    1067         5      1.0f       1069            0           0    2000   <-- vostaskus chain
13    1068         5      1.0f       1069            0           0    2000   <-- vostaskus chain
14    1069         5      1.0f       1069            0           0    2000   <-- vostaskus chain
15     281         6      1.0f        414            1           0    2000   <-- flag=1, path A
16     282         6      1.0f        414            1           0    2000   <-- flag=1, path A
17     283         6      1.0f        414            1           0    2000   <-- flag=1, path A
18     284         6      1.0f        414            1           0    2000   <-- flag=1, path A
19    1131         6      1.0f        414            1           0    2000
20    1388 …       6      1.0f        414            1           0    2000   <-- 1388..1392
24    1392
25     401         6      1.0f        413            0           0    1500   <-- table-driven bake
26     402         6      1.0f        414            0           0    1500   <-- 402 -> 414
27     403         6      1.0f        415            0           0    1000   <-- 403 -> 415
28     404         6      1.0f        416            0           0    2000
29     405         6      1.0f        417            0           0    1000
30     406         6      1.0f        418            0           0    1000
31     364         6      1.0f        402            0           0    1000
32     365         6      1.0f        402            0           0    1000
33-48  407..418, 366..369, 413..418      <-- continuation of kiln-style bake chains
49     476         2        0           0            0           0       0
50     276         3        0           0            0           0       0
51     468         7        0           0            0           0    1000
52     470         7        0           0            0           0    1000
53     273         7        0           0            0           0    1000
54     274         7        0           0            0           0    1000
55     275         7        0           0            0           0    1000
56     245         8        0           0            0           0    1000
57     272         8        0           0            0           0    1000
58     241         8        0           0            0           0    1000
[ terminator: first int == 0 ]
```

(Reproduce live with the dump script in §"How to dump this table from a future build" below.)

## What the table tells us — by recipe pattern

### Bloomery (path A — recipe-driven, output must be metallic)

Items whose process descriptor has `flag=1` (rows 15–24) ride path A. These are the **iron-ore family** (281, 282, 283, 284 etc.) that the bloomery accepts. Any item whose own type doesn't appear in the table at all also routes to path A by default. Path A's output filter is hardcoded: must descend from `0xCE`=206 (Metallic processed materials) or `0xD5`=213 (Metallic manufactured products). This is the bloomery whitelist we already documented.

### Kiln (path C, kind = 6, table-driven baking)

Rows with `kind=6` and `flag=0` (rows 25–48) describe **kiln transformations**. The mapping `input typeId → output typeId` is embedded directly in the row (`+0xC`). `+0x18` is the minimum kiln temperature required (1000–2000).

Reading the rows: **401→413, 402→414, 403→415, 404→416, 405→417, 406→418** look like one full set of "unfired thing → fired thing" pairs, and **407→413, …, 412→418** is another set, and **413→413, 414→414, …** look like terminal "already fired" rows. Likely the LiF clay-item chain: unfired bowl → fired bowl, unfired plate → fired plate, etc. across quality tiers.

**This is why the user's unfired_clay_brick → fired_clay_brick recipe vanishes silently.** When the user added their new ore/brick types in the DB, those new type IDs are nowhere in this table. So:

1. `FUN_1401db7c0(unfired_clay_brick)` walks the table and finds **no matching `hasParent` row** → returns `null`.
2. With `lVar13 == null`, recalcTick takes **Path A** (recipe-driven), not Path C (which is what the kiln logic actually needs).
3. Path A looks up the user's recipe → output is `fired_clay_brick` → checks `hasParent(fired_clay_brick, 0xCE) || hasParent(fired_clay_brick, 0xD5)` → fails (clay isn't metallic).
4. All recipe iterations fail → `FUN_14029daa0` aborts the slot → input is consumed, nothing produced.

The **fix** is one of:

- **Make the new types descend from an existing table row's typeId.** E.g., set the new `unfired_clay_brick.ParentID` chain to climb up to `401` (or any other clay-baking type already in the table). Then `hasParent(unfired_clay_brick, 401, 100)` returns true and the descriptor row applies; the `+0xC = 413` mapping kicks in. But that hard-codes the output to `413` (the existing fired type) — not your new fired_clay_brick.
- **Add a new row to the table.** This requires either a binary patch or a LiFx hook on `FUN_1401db7c0` that consults an extension table first. Recommended LiFx approach in §"How to add new furnace recipes via LiFx" below.

### Vostaskus / Damascus steel cycle (path C, kind = 5)

Three rows, all pointing at type **1069** as the eventual output:

| Input | Kind | OutputType | Temp |
|---|---|---|---|
| 1067 | 5 | 1069 | 2000 |
| 1068 | 5 | 1069 | 2000 |
| 1069 | 5 | 1069 | 2000 |

This is the multi-stage forging chain. Run `SELECT ID, Name FROM objects_types WHERE ID IN (1067,1068,1069)` to confirm — likely "Vostaskus billet stage A / stage B / stage C" or similar.

**The heat/cool/heat state machine** decoded from `recalcTick`:

```
                                    ┌─────────────────────────────┐
                                    │  player presses bellows /   │
                                    │  adds fuel  → state = 2000  │
                                    │  (HOT)                      │
                                    └──────────────┬──────────────┘
                                                   │
                                                   ▼
        slot.quality  ─────────────────────────  hot ticks (dt_scaled == 10)
                                                   │
                  0  ──[hot]──▶  20                │
                                                   │ (player banks fire / stops adding fuel)
                                                   ▼
                                    ┌─────────────────────────────┐
                                    │  state = 500 (COOL)         │
                                    └──────────────┬──────────────┘
                                                   │
                                                   ▼
                                                 cool ticks
                 20  ──[cool]─▶  40                │
                                                   │ (player rebuilds fire)
                                                   ▼
                                                  hot ticks
                 40  ──[hot]──▶  60                │
                                                   │ (cool again)
                                                   ▼
                                                  cool ticks
                 60  ──[cool]─▶  80                │
                                                   │ (hot again)
                                                   ▼
                                                  hot ticks
                 80  ──[hot]──▶ 100  (FINISHED)
```

So the player must run **five state transitions** alternating hot/cool/hot/cool/hot to finish a vostaskus billet. Each transition advances `quality` by exactly 20 points. The trigger `dt_scaled == 10` means the multiplier-scaled delta has to equal exactly 10 — i.e., precise tick timing. Hot ticks at slow rate, cool ticks at slow rate.

### Applying heat/cool/heat to a new recipe

To make a new recipe use the same multi-stage cycle, you need a row with `kind=5` for the input type and `outputTypeId` pointing at the desired final product. Concretely:

```
(yourTypeId, 5, 0x3F800000 /* 1.0f */, yourOutputTypeId, 0 /* flag=0 */, 0, 2000)
```

Adding this means the input item, when in a furnace that supports state=2000/state=500 transitions, runs the 5-step cycle. The furnace must be a `WorkingFurnace` subclass (bloomery, kiln) and must have the ability to be cooled — which seems specific to the bloomery/forge based on how the bellows ability works.

You cannot add this row by editing the DB. The table lives in `.rdata` (read-only data section) of the executable. Options:

- **Binary patch** of `ddctd_cm_yo_server.exe` to extend the table or replace entries. Doable but fragile across re-installs.
- **LiFx hook on `FUN_1401db7c0`** that consults an extension table first, then falls back to the original. Clean and reversible. See below.

## Craftwork class family — full coverage matrix

`WorkingFurnace` is one of **six concrete craftwork classes** under the same parent `AbstractCraftworkFurnace`. They all share the descriptor-table lookup at `0x1DB7C0` (so the LiFx proc-desc hook covers them all in one place), but each has its **own** vtable slot 3 — meaning a brand-new `kind` value has to be implemented per class via a separate `recalcTick` hook.

| C++ class | recalcTick (slot 3) RVA | Gameplay surface | LiFx hook today |
|---|---|---|---|
| `WorkingFurnace` | `0x1DCFF0` | Bloomery, kiln, smelter | ✅ wired |
| `BrewingTankFurnace` | `0x1DB370` | Brewing tank, fermenter | ✅ wired |
| `WorkingFire` | `0x1DB650` | Campfire / hearth (cooking) | available — not wired |
| `WorkingGreenhouse` | `0x1DEA70` | Greenhouse — plant growth | available — not wired |
| `WorkingTrap` | `0x1DEE90` | Animal traps | available — not wired |
| `WorkingWindmill` | `0x1DFF20` | Windmill — flour grinding | available — not wired |

### When you need a per-class hook vs when the proc-desc hook is enough

- **Proc-desc hook ONLY** — use this whenever your new item fits an existing `kind` value (1–8). The proc-desc table is shared, so adding a row in `kExtensionRows[]` with `kind=5` (vostaskus-style heat/cool/heat) makes that item work in any class that already handles `kind=5`. WorkingFurnace's existing switch handles 1, 2, 3, 4, 5, 6 — those are all usable today across bloomery/kiln/smelter without any tick hook.
- **Per-class recalcTick hook** — required when you want to define a brand-new `kind` value (≥100 by our convention) that the engine's hardcoded switch doesn't know about. Then you need the hook on whichever craftwork class is hosting the item.

### Adding the other four hooks

If/when you need them, the pattern is identical to `hook_brewing_tank_tick.{h,cpp}` — copy that file, change four things:

1. RVA in the file header comment + `cm_offsets.h` constant.
2. Trampoline name: `_<Class>_RecalcTick`.
3. Handler namespace: `Hooks::<Class>::RecalcTick`.
4. Attach/detach lines in `cm_server.cpp`.

Total per added hook: ~50 lines of mostly-comment scaffolding. Decompile-then-hook order: get the engine's slot-3 source (already in `/tmp/lifx_ghidra/decompile/` after running the existing scripts), read it to learn which existing `kind` values it handles, then your hook's pre-delegate dispatcher knows which kind values it can add without conflicting.

## How to add new furnace recipes via LiFx

`FUN_1401db7c0(itemTypeInfo)` is the dispatch lookup. Hook it. On call, walk your own extension table (loaded from `lifxpluss.xml` or a separate `furnace_recipes.xml`) and return your row's pointer if a `hasParent` match succeeds; otherwise fall through to the original function. Pseudocode:

```cpp
// cm_offsets.h
PROCESS_DESCRIPTOR_LOOKUP = 0x1DB7C0,

__CM_DECL_INTERNAL(void*, __fastcall, _ProcDescLookup, void* itemTypeInfo);

struct LifxFurnaceRow {
    uint32_t typeId;          // hasParent match target
    uint32_t kind;            // 4, 5, 6 typical
    uint32_t factor;          // float bits; usually 0x3F800000
    uint32_t outputTypeId;
    uint32_t flag;            // 0 = use kind branch directly
    uint32_t field5;
    uint32_t tempThreshold;
};

static std::vector<LifxFurnaceRow> g_lifxExtension;

void* Hooks::Gameplay::ProcDescLookup(void* itemTypeInfo) {
    for (auto& row : g_lifxExtension) {
        // Type::hasParent at 0x27EB30
        typedef int (*pfn_hasParent)(void*, int, int);
        static auto hasParent = (pfn_hasParent)((uintptr_t)GetModuleHandle(NULL) + 0x27EB30);
        if (hasParent(itemTypeInfo, row.typeId, 100))
            return &row;
    }
    return _ProcDescLookup(itemTypeInfo);
}
```

Define your rows in XML at server startup:

```xml
<furnace_recipes>
    <recipe typeId="9001" kind="6" output="9002" temp="1000"
            comment="unfired clay brick -> fired clay brick"/>
    <recipe typeId="9003" kind="5" output="9005" temp="2000"
            comment="custom damascus-style cycle"/>
</furnace_recipes>
```

Parse on `Lifx::Server::Init`, populate `g_lifxExtension`, and attach the hook. The unfired clay brick will now have a descriptor row → `recalcTick` enters Path C → `kind=6` branch → output 9002 spawns when the kiln finishes. Same recipe for vostaskus-style: `kind=5` and the heat/cool/heat cycle just works.

### One caveat about `+0xC` (outputTypeId)

We've been assuming this field IS the output type ID that gets spawned when the cycle finishes. The evidence is strong but indirect: it lines up with the 401→413, 402→414 etc. pattern that visually matches clay-brick quality tiers; the field is consulted *somewhere* (we haven't fully traced where). To be 100% certain it's the output type and not, say, a "completed quality cap" reference, decompile the function that consumes a fully-finished slot (quality=100). That function probably reads `lVar13+0xC` (or +0x18+0xC, doesn't matter) and spawns an instance of that type.

A direct way to verify: set up a row in your extension table with `typeId=<test input>, kind=6, output=<distinctive test output>`, finish it in a kiln, and observe what spawns. Cheaper than another decompile.

## How to dump this table from a future build

A tiny utility script in case Bitbox ever does ship an update (or you want to verify on a different build):

```python
# scripts/dump_furnace_table.py
import struct
data = open('ddctd_cm_yo_server.exe', 'rb').read()
e_lfa = struct.unpack_from('<I', data, 0x3c)[0]
nsec = struct.unpack_from('<H', data, e_lfa+4+2)[0]
oh = struct.unpack_from('<H', data, e_lfa+4+16)[0]
sec = e_lfa+4+20+oh
target = 0xacfa60   # update if .rdata layout shifts
for i in range(nsec):
    so = sec + i*40
    va = struct.unpack_from('<I', data, so+12)[0]
    vs = struct.unpack_from('<I', data, so+8)[0]
    ra = struct.unpack_from('<I', data, so+20)[0]
    if va <= target < va+vs:
        fo = ra + (target-va); break
i = 0
while True:
    row = struct.unpack_from('<7I', data, fo + i*28)
    if row[0] == 0: break
    print(f'{i:>2} {row}')
    i += 1
```

## Decompilation next step

To find the exact recipe-tree walking logic, open the saved Ghidra project and read FUN_1401db840:

```bash
~/.local/share/ghidra/ghidraRun
# Open ~/ghidra_projects/LiF.rep, double-click ddctd_cm_yo_server.exe,
# Ctrl-Shift-E (or Window → Decompiler), then g → 1401db840 to jump.
```

What to look for inside `Recycle`:

- An indexed lookup against `CmRecipesManager`'s recipe table by material type ID.
- A loop over "child" recipes — that's the recursion.
- Calls to inventory-mutation helpers (consume input quantity, spawn output object).
- An emission of one of the 5 error strings (great anchors for understanding the control flow).

Once those are identified, the rest of the bloomery hook surface (slots 8/9/11 of WorkingFurnace, the unknown ones) becomes much easier to label.
