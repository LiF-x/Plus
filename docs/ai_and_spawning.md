---
title: AI behaviors & spawning
status: verified
domain: reverse-engineering
tags: [ai, behavior-tree, spawning, animation, triggers]
related: [animal_spawn.md, animal_data_model.md, character_ai_re.md]
updated: 2026-06-26
---

# AI Behaviors and Spawning

A complete map of the LiF AI behavior-tree system (42 nodes), the animal spawn manager, the Torque-standard trigger volumes, and concrete recipes for "spawn entity in zone, play animation on spawn".

## 1. The AI behavior-tree system at a glance

LiF runs every AI-controlled creature on a Torque-style behavior tree. Trees are authored as XML files, loaded by the **`BehaviorsManager`** singleton, and built from a fixed set of C++ node classes under the namespace `AI::Nodes::*`. The tree is "ticked" per frame; each node returns success / failure / running and the composites decide what to evaluate next.

The system is **already extensively script-exposed** — you can register new behavior XMLs, hot-reload them, and inspect loaded trees from TorqueScript without touching C++:

| TorqueScript function | RVA | Purpose |
|---|---|---|
| `initBehaviorsManager()` | `0x259F0` | Construct the singleton; must run once at boot. |
| `registerBehavior(name, fileName)` | `0x25AF0` | **Bind a behavior-tree XML to a logical name.** This is the primary extension point — your custom behavior file gets loaded under the name you choose. |
| `reloadBehaviorXml()` | `0x25B70` | Hot-reload all registered XMLs. No restart needed. |
| `showBehaviorTrees()` | `0x25BF0` | Print all loaded trees (debug). |
| `printBehaviorTree(name)` | `0x25A70` | Print a specific tree (debug). |

So a "minimal mod" — adding a behavior tree composed entirely of the 42 existing node types — needs zero LiFx C++ work. Only when you need a *new node primitive* (a leaf action or condition that isn't in the catalog below) do you need to go to C++.

The XML loader is at `FUN_140153b80` (logs `Loading AI behaviour from [%s]`); the per-node factory is `AI::BehaviorTree::_createNode` at `FUN_140153d20`. Source paths in the strings reveal the layout:

- `engine/source/ai/behaviorsmanager.cpp`
- `engine/source/ai/behavior_tree/aitree.cpp`
- `engine/source/ai/behavior_tree/ainode.cpp`

Node sets are registered by *modules* at startup. Four are baked in:

- `CommonBehaviorNodes` (`FUN_1401513a0`) — composites/decorators + generic leaves usable by any creature.
- `AnimalBehaviorNodes` (`FUN_14018e840`) — animal-specific actions.
- `NPCBehaviorNodes` — human-NPC actions (combat, dialogue, etc.).
- `HorseBehaviorNodes` — horse-specific actions.

## 2. The 42 behavior nodes

All under the `AI::Nodes::` namespace. Grouped by role:

### 2.1 Tree structure (composites)

These have child nodes; they decide which child to evaluate.

| Node | Role |
|---|---|
| `Sequence` | Tick children in order; fail on first failing child, succeed when all succeed. Classic AND. |
| `Selector` | Tick children in order; succeed on first succeeding child, fail when all fail. Classic OR / fallback. |
| `DynSelector` | Re-prioritizing selector — re-evaluates higher-priority children every tick rather than committing to a running child. Use when conditions can become true again mid-action (e.g., "an enemy appeared — drop what you're doing and attack"). |
| `RandomSelector` | Pick one child at random; tick only that one. Used for varied idle behaviors. |

### 2.2 Decorators (single-child modifiers)

| Node | Role |
|---|---|
| `Inverter` | Flip success ↔ failure on the child's result. |
| `Successor` | Force success regardless of child outcome ("always succeed"). |
| `Failurator` | Force failure regardless of child outcome ("always fail"). |
| `Interrupt` | Conditional termination — abort the child if a condition becomes true. |
| `ContinuousNode` | Keep ticking the same child every frame even if it would normally stop on return (used for long-running actions). |

### 2.3 Conditions (predicates — return success/failure without acting)

| Node | Checks |
|---|---|
| `HasTarget` | Creature has a current target. |
| `EnemyInRange` | Enemy is inside attack range. |
| `EnemyOutOfReach` | Enemy is too far to engage. |
| `EnemyInAttackSector` | Enemy is within attack arc (front cone). |
| `DistanceToTargetLess` | Current target is within parameter distance. |
| `HpPercentageIsAbove` | Self HP% above parameter threshold (`AI::Nodes::HpPercentageIsAbove::process` `0x194980`). |
| `AggressionStateCheck` | Aggression-state machine is in parameter state. |
| `IsStandingOnProhibitedLand` | Standing on terrain the creature mustn't be on (e.g., player-claimed land for wild animals). |
| `LastReceivedDamageCategory` | Last hit's damage *category* matches parameter (`0x194c40` for the type variant). |
| `LastReceivedDamageType` | Last hit's damage *type* matches parameter. |
| `ThreatValueGreater` | Threat accumulator above threshold. |
| `Damaged` | Took damage this tick (`AI::Nodes::Damaged::process` `0x193c70`). |

### 2.4 State mutators (set internal state, return success)

| Node | Effect |
|---|---|
| `SetAggressionState` | Switch the aggression FSM into the parameter state. |
| `SetPlayerAsTarget` | Lock onto a specific player as the current target. |
| `ResetTarget` | Drop the current target. |
| `ClearEnemyInteractions` | Wipe the threat / interaction history. |
| `ThreatValueChange` | Add/subtract from the threat accumulator. |

### 2.5 Action leaves (do things in the world)

| Node | Action |
|---|---|
| `Move` | Move toward a parameter position/direction. |
| `GoToPosition` | Pathfind to a parameter position. |
| `ChaseEnemy` | Path toward current target while playing the chase animation. |
| `ChaseEnemyNoAnimation` | Same, but suppress the chase anim (used for stealthier creatures). |
| `Attack` | Execute the current attack ability against the current target. |
| `Flee` | Path away from current threat. |
| `FollowOwner` | Pet/horse follow behavior — track and approach owner. |
| `AimAtClosestEnemy` | Rotate to face the nearest enemy. |
| `Stopped` | Halt movement and zero velocity. |
| `Wait` | Idle for parameter duration. |
| `PlayAnimation` | Start playing parameter animation name (looping if applicable). `0x18FE60` |
| `PlayAnimationTimed` | Start playing parameter animation for parameter duration. |
| `ResetAnimation` | Stop the current animation override, return to default. `0x194E10` |
| `AbilityAnimationPerform` | Trigger the animation associated with the current ability — useful in combat sequences where the ability already defines its own anim. |
| `Death` | Run the death sequence (animation + ragdoll handoff + cleanup). |

### 2.6 Special

| Node | Notes |
|---|---|
| `INode` | Abstract base — every node inherits from this. Defines `getAiData()` (`AI::Nodes::INode::getAiData` `0x1530D0`). Never instantiated directly in XML. |

## 2.7 Behavior-tree XML schema (verified from the binary)

Source confirmed by decompiling the loader at `FUN_140153B80` and the node factory `AI::BehaviorTree::_createNode` at `FUN_140153D20`. XML library used: **TinyXml1** (note: not tinyxml2 — same author, older API).

### Root and shape

A behavior tree file contains exactly **one root `<node>` element** at the top of the document. There's no outer `<behavior>` / `<tree>` wrapper. The root node is itself a full behavior-tree node and is the entry point evaluated each tick.

```xml
<node class="Sequence" name="wolf_main">
    <node class="PlayAnimationTimed" name="emerge"/>
    <node class="Selector">
        <node class="Sequence">
            <node class="HasTarget"/>
            <node class="EnemyInRange"/>
            <node class="Attack"/>
        </node>
        <node class="DynSelector">
            <node class="Sequence">
                <node class="Damaged"/>
                <node class="Flee"/>
            </node>
            <node class="Wait"/>
        </node>
    </node>
</node>
```

### Universal attributes on every `<node>`

These three attributes are read by `_createNode` itself and apply to every node regardless of `class`:

| Attribute | Required? | Meaning |
|---|---|---|
| `class` | **yes** | One of the 42 node class names from §2.1–§2.6 (e.g., `"Sequence"`, `"PlayAnimation"`, `"EnemyInRange"`). The factory looks this up in the registered-node table and instantiates the matching C++ class. If not found, the loader bails. |
| `name` | no | A debugging label. Surfaces in `printBehaviorTree(treeName)` output. Has no functional effect at runtime. |
| `decorator` | no | A decorator class name (also from the 42). If present, the created node is wrapped in this decorator before being returned to its parent. Equivalent to nesting the node inside a decorator manually, but compact. Common values: `"Inverter"`, `"Successor"`, `"Failurator"`, `"Interrupt"`, `"ContinuousNode"`. |

### Children

Children are nested `<node>` elements. `_createNode` walks them with `firstChild("node")` / `nextSibling("node")` and recursively calls itself, attaching each child to the parent's child list. **Element name is literally `"node"`** — using `<sequence>` or `<attack>` etc. as the tag name will not work; everything is `<node>` and the `class` attribute is what discriminates.

### Per-node-class attribute: always called `value`

`_createNode` only reads the three universal attributes itself. Anything additional is read by the **node-class-specific loader at vtable slot 1**. We decompiled slot 1 of **all 42 node classes** and the pattern is dead uniform:

> **Every parameterized node reads exactly one XML attribute, called `value`.** What changes between classes is *how the value's string is parsed* — raw, single number, or space-delimited list.

So the universal form of any parameterized node in your XML is:

```xml
<node class="ClassName" value="..."/>
```

The 42 nodes split cleanly into four groups by parameter shape:

#### A. No parameters — 24 nodes (slot-1 is a 9-line shared no-op)

These take no `value` attribute. Pure structural / behavioral primitives:

| Group | Nodes |
|---|---|
| Composites | `Sequence`, `Selector`, `DynSelector` |
| Decorators | `Inverter`, `Successor`, `Failurator`, `Interrupt`, `ContinuousNode` |
| Stateless conditions | `HasTarget`, `EnemyInAttackSector`, `EnemyOutOfReach`, `Damaged`, `IsStandingOnProhibitedLand` |
| Stateless actions | `Stopped`, `Flee`, `FollowOwner`, `Death`, `AimAtClosestEnemy`, `ChaseEnemy`, `ChaseEnemyNoAnimation`, `ResetAnimation`, `ResetTarget`, `ClearEnemyInteractions` |
| Abstract | `INode` (never used directly) |

XML form: `<node class="Sequence"/>` with optional children. No `value=` attribute.

#### B. Single raw string in `value` — 4 nodes

The value is taken verbatim (no parsing) and stored as a string member of the node.

| Node | `value` is |
|---|---|
| `PlayAnimation` | Animation name (e.g., `value="emerge_idle"`). Passed to `NPCS::AnimatedNPC::setAnimation`. |
| `AbilityAnimationPerform` | Ability or animation identifier. |
| `Attack` | Attack identifier (script-side meaning). |
| `GoToPosition` | Destination spec — could be a named anchor or coordinate-style string. |

XML form: `<node class="PlayAnimation" value="wolf_emerge"/>`.

#### C. Single number in `value` — 4 nodes (share loader at `0x18F4F0` or use `strtol`)

The value is parsed by `strtod` (or `strtol` for `Wait`) and stored as one float (or int) at the node's `+0x40` slot.

| Node | `value` units / meaning |
|---|---|
| `Wait` | Integer seconds (parsed by `strtol`, base 10). |
| `HpPercentageIsAbove` | HP percentage threshold (e.g., `value="50"`). |
| `DistanceToTargetLess` | Distance threshold (units). |
| `ThreatValueGreater` | Threat threshold. |

XML form: `<node class="Wait" value="3"/>` or `<node class="HpPercentageIsAbove" value="50"/>`.

#### D. Space-delimited list in `value` — 9 nodes

The value is split by whitespace (delimiter at `DAT_140736A30 = " "`, same delimiter used by the spawn-pattern `count` / `quality` attributes) and each token is parsed individually. Six of these store a vector of `float`s (via `strtod`); two store a vector of category/type *strings*; one is mixed.

| Node | Parsing pattern | Likely shape |
|---|---|---|
| `EnemyInRange` | Two floats stored at `+0x40` / `+0x44`. | `value="min max"` — bracket of in-range distances. |
| `Move` | Vector of floats. | Likely direction vector or speed parameters (decompile to confirm). |
| `ThreatValueChange` | Vector of floats. | Likely an amount value (positive or negative). |
| `SetPlayerAsTarget` | Vector of floats. | Likely a radius / selection criteria. |
| `RandomSelector` | Vector of floats. | Likely weights for each child's selection probability. |
| `AggressionStateCheck` | One value tested as string. | Aggression-state name (`value="aggressive"`). |
| `SetAggressionState` | Same — single state name. | `value="passive"`. |
| `LastReceivedDamageCategory` | Vector of *strings* (no `strtod`). | Set of category names to match (`value="physical magic"`). |
| `LastReceivedDamageType` | Vector of *strings*. | Set of type names to match. |
| `PlayAnimationTimed` | First token = string, remaining tokens = floats. | `value="anim_name 2.5"` — animation name plus duration. |

XML examples:

```xml
<node class="EnemyInRange" value="2.0 8.0"/>            <!-- in range 2–8 units -->
<node class="PlayAnimationTimed" value="emerge 2.5"/>    <!-- play 'emerge' for 2.5s -->
<node class="LastReceivedDamageType" value="slash pierce"/> <!-- match either type -->
<node class="SetAggressionState" value="aggressive"/>
```

### Complete worked example

```xml
<node class="Sequence" name="wolf_full_tree">

    <!-- intro animation on first tick after spawn -->
    <node class="PlayAnimationTimed" value="emerge 2.5"/>

    <!-- main loop -->
    <node class="DynSelector">

        <!-- if low HP, flee -->
        <node class="Sequence">
            <node class="HpPercentageIsAbove" value="25" decorator="Inverter"/>
            <node class="Flee"/>
        </node>

        <!-- if enemy nearby, attack -->
        <node class="Sequence">
            <node class="HasTarget"/>
            <node class="EnemyInRange" value="0.5 3.0"/>
            <node class="Attack" value="bite"/>
        </node>

        <!-- if enemy far but visible, chase -->
        <node class="Sequence">
            <node class="HasTarget"/>
            <node class="EnemyInRange" value="3.0 25.0"/>
            <node class="ChaseEnemy"/>
        </node>

        <!-- otherwise wander/idle -->
        <node class="RandomSelector" value="3 1">
            <node class="Wait" value="5"/>
            <node class="GoToPosition" value="wander"/>
        </node>
    </node>
</node>
```

### How to read a node's exact `value` shape if uncertain

The per-node decompiles are saved under `/tmp/lifx_ghidra/decompile/ai_nodes/<NodeName>_slot1.c`. Inside, look for:

- `FUN_14045a920(xml, "value", 0)` — fetches the attribute.
- `FUN_140458130(str, &DAT_140736A30, &out)` — splits by space into a vector.
- `strtod` / `strtol` — converts string tokens to numbers.
- `FUN_14008fa00(node+0x48, ...)` — stores a string into the node.

The pattern in those three primitives tells you the shape — string vs. number vs. list — and at what offset the value lands in the node struct. Cross-reference with the corresponding `<NodeName>_slot2.c` (the `process` method) to see how the value is consumed each tick.

## 3. Extending the tree — registering new node types

Confirmed end-to-end by decompiling `_CommonBehaviorNodes::init` and `_AnimalBehaviorNodes::init`. The engine's node factory follows the **prototype pattern**: register one instance under an XML name, the engine clones it on every `<node class="…"/>` it parses.

### 3.1 The registration API

| Function | RVA | Purpose |
|---|---|---|
| `AI::BehaviorTree::getNodeFactory` | `0x153860` | Returns the global node-factory singleton — a hash-map of XML name → prototype `INode*`. Lazy-initialized on first call. |
| `AI::BehaviorTree::registerNode` | `0x153950` | `register(factory, "XmlClassName", &prototypePtr)`. Inserts/replaces the entry. |
| `AI::BehaviorTree::createByName` | `0x153760` | What `_createNode` uses — looks up the name, clones the prototype via vtable slot 2 of the registered entry. Hash with `FUN_140457740`; string compare with `FUN_140457190`. |

C++ shape of `register` extracted from call sites:

```cpp
typedef void  (*pfn_registerNode)(void* factory, const char* xmlClassName, INode** prototype);
typedef void* (*pfn_getNodeFactory)();
```

### 3.2 Anatomy of a built-in registration

Excerpt from `_CommonBehaviorNodes::init` (`0x1513B0`) registering `Sequence`:

```cpp
void* factory = getNodeFactory();              // 0x153860
INode* p = (INode*)operator_new(0x40);
new (p) AI::Nodes::INode();                    // base ctor
p->reserve_internal_buffer();                  // FUN_1404551D0
*(void**)p = AI::Nodes::Sequence::vftable;     // swap vtable to derived
registerNode(factory, "Sequence", &p);         // 0x153950
```

Two facts worth knowing for extension work:

1. **XML class name ≠ C++ class name** is supported and used in the wild. From `_AnimalBehaviorNodes::init`:
   ```cpp
   prototype = new HpPercentageIsAbove();
   prototype->valueField = 0;
   registerNode(factory, "HpLessCheck", &prototype);
   ```
   So `<node class="HpLessCheck" value="…"/>` is a valid behavior-tree element even though there's no `AI::Nodes::HpLessCheck` C++ class — it's `HpPercentageIsAbove` under another XML name with a different default. **The full catalog of valid `class=` strings is larger than the 42 RTTI classes.** Decompiling all four `_*BehaviorNodes::init` slot-1 methods and tallying every `registerNode("Name", …)` call gives the exhaustive name list.

2. **Same C++ class can be registered multiple times** under different XML names, each with different prototype defaults. Useful for creating species-specific tuning presets without a new class.

### 3.3 Adding your own node via LiFx — recipe

Three pieces:

#### Step 1 — Define the C++ class

Derive from `AI::Nodes::INode` (or any existing node if you only want a parameter-handling variant). Match the existing ABI: vtable slot 1 is the XML loader, slot 2 is the per-tick `process`. Templates are on disk at `/tmp/lifx_ghidra/decompile/ai_nodes/*_slot{1,2}.c`.

```cpp
namespace LifxAI {
    class FaceTowardSpawnZone : public AI::Nodes::INode {
    public:
        bool loadFromXml(TiXmlElement* x) override {
            const char* v = x->Attribute("value");
            if (!v) return false;
            zoneName_ = v;
            return true;
        }
        // Return code semantics from existing nodes: 1=success, 2=failure, 3=running
        unsigned process(void* ctx) override {
            // your logic — call NPCS::AnimatedNPC::setAnimation, aim helpers,
            // threat helpers etc. via the offsets we've already mapped.
            return 1;
        }
    private:
        std::string zoneName_;
    };
}
```

Note: existing nodes store their `value` parameter at `+0x40` (numeric) or `+0x48` (string). Match those offsets when subclassing, or your `process` reads garbage from the wrong slots.

#### Step 2 — Resolve the registration functions

```cpp
typedef void* (*pfn_getFactory)();
typedef void  (*pfn_registerNode)(void* factory, const char* name, void** proto);

static pfn_getFactory   g_getFactory   = nullptr;
static pfn_registerNode g_registerNode = nullptr;

void LifxAI::ResolveRegistration() {
    auto base = (uintptr_t)GetModuleHandleW(nullptr);
    g_getFactory   = (pfn_getFactory)   (base + 0x153860);
    g_registerNode = (pfn_registerNode) (base + 0x153950);
}
```

#### Step 3 — Register on a post-`BehaviorsManager` hook

Two equivalent hook points; pick whichever is more comfortable:

- **Module-system canonical:** hook vtable slot 2 of `_BehaviorsManager::_ModuleInst` (`0x14FEB0`). The Torque ModuleManager calls it right after `BehaviorsManager` is constructed.
- **Easier-to-bind:** hook `BehaviorsManager::reloadBehaviorXML` (`0x1506D0`) and wrap the registration in `std::call_once` — the first time script invokes `reloadBehaviorXml()` your custom nodes get registered, then the normal reload proceeds.

```cpp
void Hooks::Gameplay::BMInit_after(void* self) {
    LifxAI::ResolveRegistration();
    void* factory = g_getFactory();

    void* proto = new LifxAI::FaceTowardSpawnZone();
    g_registerNode(factory, "FaceTowardSpawnZone", &proto);

    // ...more registrations...

    _BMInit_after(self);
}
```

#### Step 4 — Use it from XML

```xml
<node class="Sequence" name="wolf_with_spawn_zone_lookup">
    <node class="PlayAnimationTimed" value="emerge 2.5"/>
    <node class="FaceTowardSpawnZone" value="village_square"/>
    <!-- ...rest of the tree... -->
</node>
```

### 3.4 The canonical registered-XML-name catalog

Extracted by decompiling all four built-in module-init functions and tallying every `registerNode(factory, "name", &proto)` call (resolving both inline string literals and `&DAT_*` references). **40 XML class names registered across 39 distinct C++ classes** — meaning two XML names are aliases for an underlying class that isn't reachable under its own RTTI name.

The four modules and their init function entry points (`vtable slot 1` of each `_ModuleInst`):

| Module | Init RVA | Count |
|---|---|---|
| `_CommonBehaviorNodes::init` | `0x1513B0` | 8 |
| `_AnimalBehaviorNodes::init` | `0x18E8A0` | 27 |
| `_NPCBehaviorNodes::init` | `0x2E5FC0` | 3 |
| `_HorseBehaviorNodes::init` | `0x3E9210` | 2 |

Full table — `XML class name` is what you write in the `class="..."` attribute, `C++ class` is the RTTI name of the actual implementation:

| Module | XML class name | C++ class | Notes |
|---|---|---|---|
| Common | `Selector` | `Selector` | |
| Common | `Sequence` | `Sequence` | |
| Common | `RandomSelector` | `RandomSelector` | |
| Common | `DynSelector` | `DynSelector` | |
| Common | `Interrupt` | `Interrupt` | |
| Common | `Inverter` | `Inverter` | |
| Common | `Successor` | `Successor` | |
| Common | `Failurator` | `Failurator` | |
| Animal | **`HpLessCheck`** | `HpPercentageIsAbove` | **Alias** — class is registered under this XML name only. |
| Animal | `Damaged` | `Damaged` | |
| Animal | `LastReceivedDamageType` | `LastReceivedDamageType` | |
| Animal | `LastReceivedDamageCategory` | `LastReceivedDamageCategory` | |
| Animal | `EnemyInRange` | `EnemyInRange` | |
| Animal | `EnemyInAttackSector` | `EnemyInAttackSector` | |
| Animal | `EnemyOutOfReach` | `EnemyOutOfReach` | |
| Animal | `IsStandingOnProhibitedLand` | `IsStandingOnProhibitedLand` | |
| Animal | `ThreatValueGreater` | `ThreatValueGreater` | |
| Animal | `AggressionStateCheck` | `AggressionStateCheck` | |
| Animal | `HasTarget` | `HasTarget` | |
| Animal | `DistanceToTargetLess` | `DistanceToTargetLess` | |
| Animal | `SetAggressionState` | `SetAggressionState` | |
| Animal | `SetPlayerAsTarget` | `SetPlayerAsTarget` | |
| Animal | `ResetTarget` | `ResetTarget` | |
| Animal | `ResetAnimation` | `ResetAnimation` | |
| Animal | `ThreatValueChange` | `ThreatValueChange` | |
| Animal | `PlayAnimation` | `PlayAnimation` | |
| Animal | `PlayAnimationTimed` | `PlayAnimationTimed` | |
| Animal | `Death` | `Death` | |
| Animal | `AimAtClosestEnemy` | `AimAtClosestEnemy` | |
| Animal | `Flee` | `Flee` | Name passed as `&DAT_14079AB54` rather than inline. |
| Animal | `ChaseEnemy` | `ChaseEnemy` | |
| Animal | `ChaseEnemyNoAnimation` | `ChaseEnemyNoAnimation` | Prototype built via helper `FUN_140190FB0` rather than direct ctor. |
| Animal | `Attack` | `Attack` | |
| Animal | `ClearEnemyInteractions` | `ClearEnemyInteractions` | Prototype built via helper `FUN_140191040`. |
| Animal | `Move` | `Move` | Name passed as `&DAT_*` reference. |
| NPC | `GoToPosition` | `GoToPosition` | |
| NPC | `Wait` | `Wait` | Name passed as `&DAT_1407E6268`. |
| NPC | **`PerformAbilityAnim`** | `AbilityAnimationPerform` | **Alias** — class is only reachable via this XML name. |
| Horse | `FollowOwner` | `FollowOwner` | |
| Horse | `Stopped` | `Stopped` | |

**Aliases summary (use these XML names; the C++ class name won't work):**

| If you mean… | Write `class=…` as |
|---|---|
| `AI::Nodes::HpPercentageIsAbove` | **`HpLessCheck`** (the only registered name) |
| `AI::Nodes::AbilityAnimationPerform` | **`PerformAbilityAnim`** (the only registered name) |

**Unreachable from XML:**

- `AI::Nodes::ContinuousNode` has a C++ class and RTTI entry but is **not registered** in any of the four built-in modules. Currently you can only use it from C++. If you want it in XML, register it from a LiFx hook with the registration recipe above.

### 3.5 Alias-only extension (no new C++ class)

If you just want "the same node but with different defaults" — e.g., a `WolfHpLow` preset that's really `HpPercentageIsAbove` with `value=20` pre-baked — you don't need new code. Same registration mechanism, existing vtable:

```cpp
auto* p = (HpPercentageIsAbove*) operator_new(/* sizeof */);
new (p) HpPercentageIsAbove();
p->setDefaultValue(20);
g_registerNode(g_getFactory(), "WolfHpLow", (void**)&p);
```

This is exactly the trick `_AnimalBehaviorNodes` uses for `HpLessCheck`.

### 3.6 Caveats worth pinning before shipping a real extension

- **`AI::Nodes::INode` vtable layout** is confirmed for slots 1 (XML load) and 2 (`process`). The other slots (0/3+) need a short decompile pass if you want to derive cleanly rather than declaring an opaque base with reserved padding. Many simple nodes use a no-op stub at `0x9DE40` for slot 2 — that's the "always return success" shortcut.
- **The `INode` data layout** above offset `+0x40` is whatever the parent class chose. If you extend an existing class instead of `INode`, mirror its field offsets exactly.
- **Decorator-shaped nodes** (single child, modify result) need their slot-1 loader to attach the child via `FUN_140152DD0` — the same "attach child" helper `_createNode` uses. Copying from `Inverter_slot1.c` or `Successor_slot1.c` is the cleanest path.

## 4. Animal spawning — `Animals::SpawnControl`

LiF has a dedicated XML-driven spawn manager for wild animals, independent of the Torque trigger system. The architecture, in order of evaluation:

```
                    AnimalFamilyInfo (XML)
                          │
                          ▼  load
                    SpawnPattern   ────────► substance / biome filters
                          │
                          ▼  generated at server start
                    SpawnPoint(s)
                          │
                          ▼  per-tick
                    processTick()  ────────► _createPotentialAnimals
                                              _removeAnimalFromSpawnPoint
```

### 4.1 Singleton + lifecycle

| Function | RVA | Role |
|---|---|---|
| `Animals::SpawnControl::SpawnControl` (ctor) | `0x1998F0` | Singleton constructor. |
| `initAnimalsSpawnControl()` *(TorqueScript)* | `0x2A1F0` | Construct the singleton; runs once at boot. |
| `Animals::SpawnControl::_loadPatternsFromXML` | `0x19C7A0` | **Read every `SpawnPattern` XML file.** |
| `Animals::SpawnControl::SpawnPattern::loadFromXml` | `0x19DD50` | Parse one pattern XML. |
| `Animals::SpawnControl::SpawnPattern::AnimalFamilyInfo::loadFromXML` | `0x19DA40` | Parse one family entry within a pattern. |
| `Animals::SpawnControl::_generateSpawnPoints` | `0x19C040` | Build the in-memory spawn-point list once XML is loaded. |
| `Animals::SpawnControl::processTick` | `0x19E310` | The driver — runs every tick to fill / cull populations. |
| `startStretchedAnimalSpawnMaintenance()` *(TorqueScript)* | `0x2A2F0` | Begin the maintenance loop. |

### 4.2 Selection helpers

`processTick` walks: pattern → cluster → substance → geo point.

| Function | RVA | Returns |
|---|---|---|
| `_selectRandomPattern` | `0x19CD00` | Pick a spawn pattern. |
| `_selectRandomCluster` | `0x19C040` | Within the pattern, pick a cluster (group of related spawns). |
| `_selectRandomSubstance` | `0x19C040` | Pick a creature substance/sub-family. |
| `_getClusterInitGeoPoint` | `0x19D630` | Initial point for a new cluster. |
| `_getClusterRandomGeoPoint` | `0x19ACD0` | Randomized point within cluster bounds. |
| `getSpawnPosition` | `0x19D630` | Final transformed position handed to the creator. |

### 4.3 Spawn-pattern XML schema (verified from the binary)

Source confirmed by decompiling `_loadPatternsFromXML` (`0x19C7A0`), `SpawnPattern::loadFromXml` (`0x19DD50`), and `AnimalFamilyInfo::loadFromXML` (`0x19DA40`). Same XML library (TinyXml1) as the behavior trees.

```xml
<spawn_patterns cluster_size="100">
    <pattern probability="50" substances="forest plains">
        <family name="WolfData" count="2 5" quality="30 80"/>
        <family name="DeerData" count="10 20" quality="40 90"/>
    </pattern>
    <pattern probability="30" substances="swamp">
        <family name="BoarData" count="3 6" quality="40 70"/>
    </pattern>
</spawn_patterns>
```

| Element / attribute | Required? | Meaning |
|---|---|---|
| `<spawn_patterns>` root element | **yes** | Loader bails with `inconsistent format` if missing. |
| `cluster_size` (on root) | **yes** | Integer parsed via `strtol`. Stored in `DAT_140ACE730` — the global spawn-cluster size. Missing → `spawn cluster size info is absent`. |
| `<pattern>` (child of root) | yes, ≥1 | Each represents one spawn rule. Iterated with `firstChild("pattern")` / `nextSibling("pattern")`. |
| `probability` (on `<pattern>`) | **yes** | Integer relative weight when randomly picking a pattern. Missing → `spawn pattern probability info is absent`. |
| `substances` (on `<pattern>`) | **yes** | Whitespace-delimited list (space / tab / newline) of substance/biome filter strings. Missing → `spawn pattern substances info is absent`. |
| `<family>` (child of `<pattern>`) | yes, ≥1 | Each represents one animal family within the pattern. |
| `name` (on `<family>`) | **yes** | Name of an `AnimalData` datablock (TorqueScript-defined). The loader does an RTTI-checked `__RTDynamicCast` to `AnimalData::RTTI_Type_Descriptor`; if the named SimObject either doesn't exist or isn't an `AnimalData`, the loader prints `No animalData with name %s` and rejects the family entry. Missing entirely → `animal family name info is absent`. |
| `count` (on `<family>`) | **yes** | **Space-delimited range** "min max" parsed as two `long`s (e.g., `count="2 5"` → spawn 2 to 5 animals). If only one token, max defaults to min. Missing → `animal family count info is absent`. |
| `quality` (on `<family>`) | **yes** | Same space-delimited "min max" range, applied to the spawned animal's quality stat (e.g., `quality="30 80"`). Missing → `animal family quality info is absent`. |

### 4.5 Where the XMLs live

Look in your LiF server install for files referenced by `_loadPatternsFromXML`. The string list around `FUN_14019c7a0` will give the canonical filenames at runtime; commonly something like `data/animals/spawn_patterns/*.xml`. (We can `objdump`/`strings` the binary again narrowed to that function if you need exact paths.)

### 4.4 The "spawn in zone" question — answered

You don't *necessarily* need a Torque Trigger to spawn animals in a specific zone. The native pattern XML already supports geo-region constraints — that's how the LiF world places wolves in forests and not in cities. Adding a new spawn rule for "this zone, this creature, this density" is **an XML edit**, not C++ surgery. Find an existing pattern, copy it, change the constraints, register the new file, and `reloadBehaviorXml()` won't help here — there's no equivalent script API exposed for `SpawnControl::reload`, so you'll need a server restart to pick it up (or LiFx can hook `_loadPatternsFromXML` and add a `reloadSpawnPatterns()` console command — see §7 below).

## 5. Torque-standard Trigger volumes

Independent of `Animals::SpawnControl` — these are level-editor placed convex hulls that fire callbacks on enter/leave.

| Symbol | RVA | Signature |
|---|---|---|
| `TriggerData::onEnterTrigger` (virtual) | `0x1E710` | `void onEnterTrigger(Trigger* trigger, GameBase* obj)` |
| `TriggerData::onLeaveTrigger` (virtual) | `0x1E790` | `void onLeaveTrigger(Trigger* trigger, GameBase* obj)` |

Both are **TorqueScript-overridable** — define `function MyTriggerData::onEnterTrigger(%this, %trigger, %obj) { … }` in script and it gets called automatically. From inside that callback you can do anything: spawn an entity, play an animation on the entering object, gate progression, etc. No C++ change required.

Related callbacks that fire automatically on Player:

| Callback | RVA |
|---|---|
| `Player::onEnterMissionArea` (virtual) | `0x14AF0` |
| `Player::onLeaveMissionArea` (virtual) | `0x14BF0` |
| `Player::onEnterLiquid` (virtual) | `0x14A70` |
| `Player::onLeaveLiquid` (virtual) | `0x14B70` |

The `MissionArea` callbacks are special — they fire when the player crosses the level's defined mission boundary, useful for "you've left the playable zone" warnings.

## 6. The animation pipeline

The `PlayAnimation` AI node ultimately calls `NPCS::AnimatedNPC::setAnimation` at `0x2E2A90`. So if you want to play an animation on an NPC outside the behavior tree (e.g., the moment of spawn, before the tree starts ticking), call `setAnimation` directly via Detours wrapper or via a script-exposed shim.

| Symbol | RVA |
|---|---|
| `NPCS::AnimatedNPC::setAnimation` | `0x2E2A90` |
| `NPCS::AnimatedNPC::getCurrentAnimationName` | `0x2E25F0`, `0x2E2790` |
| `AI::Nodes::PlayAnimation::process` | `0x18FE60` |
| `AI::Nodes::ResetAnimation::process` | `0x194E10` |
| `AttackAnimationDataManager::loadFromXml` | `0xB6500` |

The attack-anim data is its own XML system (`AttackAnimationDataManager`). Generic NPC animations are likely loaded as part of the creature datablock.

## 7. Recipe — "spawn an animal in a zone with a custom intro animation"

Three independent paths. Pick by whether you want the spawn driven by a level trigger (player entered) or a global rule (population maintenance):

### Path A — entirely in TorqueScript, zero LiFx changes

For "when a player enters this zone, spawn a wolf that plays a 'birth' animation."

1. **Author a behavior tree XML** that starts with the spawn animation:
   ```xml
   <Sequence>
     <PlayAnimationTimed name="wolf_spawn_emerge" duration="2.5"/>
     <!-- now hand off to the wolf's normal idle/aggression tree -->
     <Selector>
       <Sequence>
         <HasTarget/>
         <EnemyInRange/>
         <Attack/>
       </Sequence>
       <RandomSelector>
         <Wait duration="3"/>
         <GoToPosition target="random"/>
       </RandomSelector>
     </Selector>
   </Sequence>
   ```
2. **Register it** from server-start script:
   ```ts
   registerBehavior("wolf_with_spawn_anim", "data/behaviors/wolf_emerge.xml");
   reloadBehaviorXml();
   ```
3. **Place a `Trigger` volume** in the level (mission editor or .mis file) with a `TriggerData` named e.g. `WolfSpawnTriggerData`.
4. **Override the trigger callback** in script:
   ```ts
   function WolfSpawnTriggerData::onEnterTrigger(%this, %trigger, %obj) {
     // %obj is the entering object (Player). Spawn a wolf at the trigger's center.
     %wolf = spawnObject("Wolf", "WolfDataDefault");
     %wolf.setTransform(%trigger.getTransform());
     %wolf.setBehaviorTree("wolf_with_spawn_anim");
   }
   ```
   (The exact `setBehaviorTree`-equivalent script call is somewhere in the `BehaviorsManager` exports; `showBehaviorTrees()` plus a script reflection sweep will reveal the per-creature attach API. If LiF doesn't already expose it cleanly, that's the only LiFx-side line of C++ you need — a `Con::AddCommand` wrapper around the C++ "attach tree" call.)

This path is the smallest change. Server stays the stock binary, no LiFx hook required beyond the existing `[LiFx]` console marker.

### Path B — extend the animal SpawnControl XML

For "globally, in this region, spawn N wolves; each plays the emerge anim on creation."

1. **Add or edit a `SpawnPattern` XML** under the LiF install (location revealed by `_loadPatternsFromXML` strings; we can pin it precisely if needed). Constrain it to your geo region.
2. **Set the creature's default behavior tree** to the one you registered in path A. The `AnimalFamilyInfo` entry references a behavior name.
3. **Restart the server** (no script reload API exposed for spawn patterns by default — see hook proposal in §8).

### Path C — LiFx hook for direct control

If you want fine-grained per-spawn control (e.g., "different animation depending on time of day"), hook `Animals::SpawnControl::_createPotentialAnimals` (`0x19ACD0`) and, in the post-creation callback, call `NPCS::AnimatedNPC::setAnimation` directly with your chosen anim name. ~30 lines.

## 8. Proposed LiFx hooks worth adding for AI/spawn modding

Not required for the basic recipe above, but cheap quality-of-life additions:

| Hook target | RVA | Purpose |
|---|---|---|
| `Animals::SpawnControl::_loadPatternsFromXML` | `0x19C7A0` | Expose a `Con::AddCommand("reloadSpawnPatterns")` so admins can hot-reload spawn rules without a restart. |
| `Animals::SpawnControl::_createPotentialAnimals` | `0x19ACD0` | Post-spawn event surface — emit `Con::Echo("[spawn] %s at %s,%s,%s")` for every animal spawn, optionally call a TorqueScript callback. |
| `AI::BehaviorTree::_createNode` | `0x153D20` | If you want LiFx to add new node types from C++, this is the registration call to either hook (insert custom case) or wrap. |
| `BehaviorsManager::reloadBehaviorXML` *(internal)* | `FUN_1401506D0`/`FUN_1401510E0` | Hook to log every behavior reload — useful while authoring trees. |

## 9. Quick command reference

From TorqueScript at runtime (no LiFx needed):

```
showBehaviorTrees();                                  // list every loaded tree
printBehaviorTree("wolf");                            // dump a tree
registerBehavior("name", "data/behaviors/file.xml");  // register a new XML
reloadBehaviorXml();                                  // hot-reload all trees
initAnimalsSpawnControl();                            // (re)init spawn manager (rarely needed)
startStretchedAnimalSpawnMaintenance();               // begin the maintenance tick
initPlayerSpawnPoints();                              // (re)init player-only spawn points
```

If you want me to chase the XML formats more precisely — read `_loadPatternsFromXML` (`0x19C7A0`) and `SpawnPattern::loadFromXml` (`0x19DD50`) decompiles to nail down the exact element/attribute names — say the word.
