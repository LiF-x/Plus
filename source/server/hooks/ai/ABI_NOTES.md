# AI behavior-tree node ABI (RE notes, issue #119)

Reverse-engineered from `ddctd_cm_yo_server.exe` (ImageBase `0x140000000`,
so VA = `0x140000000` + RVA) with `objdump -d`. These are the facts the
`hook_behavior_node.*` implementation relies on.

## Node factory (prototype pattern)

| Symbol | RVA | Signature (verified from call sites) |
|---|---|---|
| `getNodeFactory` | `0x153860` | `void* __fastcall()` — lazy singleton; returns static factory at `0x140b7c5a0`. Idempotent (guarded), independent of `BehaviorsManager`. |
| `registerNode` | `0x153950` | `void __fastcall(void* factory, const char* name, void** proto)` — inserts/replaces; **moves ownership** (`*proto` read, then `*proto = 0`); destroys any prior prototype via its slot-0 dtor. Stores prototype at factory-entry `+0x20`. |
| `createByName` | `0x153760` | `void* __fastcall(void* factory, void** out, const char* name)` — looks up name, clones the stored prototype by calling its **vtable slot 2** (`call [vtbl+0x10]`), writes clone to `*out`. |
| `_createNode` | `0x153D20` | XML→node. Reads `class`/`name`/`decorator` attrs, calls `createByName`, then the clone's **slot 1** loader with the `TiXmlElement*`. |

## INode vtable layout (6 slots, all `__fastcall`, `this` in rcx)

Confirmed by locating known `process` addresses in `.rdata` vtables
(`HpPercentageIsAbove::process 0x194980`, `Damaged::process 0x193c70`,
`PlayAnimation::process 0x18fe60`) and walking the COL-delimited tables.

| slot | offset | method | notes |
|---|---|---|---|
| 0 | +0x00 | `void* dtor(this, unsigned flags)` | flags&1 ⇒ `operator delete`. **Class-specific layout** — destroys members (e.g. string at +0x30, child vector at +0x40/+0x50). Sizes vary per class (saw 0x40 and 0x58), NOT a flat 0x48. |
| 1 | +0x08 | `bool load(this, TiXmlElement*)` | reads the `value` attr; shared no-op stub `0x14009de40` = `mov al,1; ret` for no-param nodes. |
| 2 | +0x10 | `void clone(this, INode** out)` | deep-copies; sets the new object's vtable to the **concrete class** vtable. |
| 3 | +0x18 | `int process(this)` | per-tick. **Single arg (`this`)**; AI context via `getAiData` (`0x1530D0`). |
| 4 | +0x20 | base method (`0x1531a0`/`0x1531c0`) | shared across classes. |
| 5 | +0x28 | base method (`0x152fd0`) | shared across classes. |

`process` return codes: **2 = failure** (Damaged returns `eax=2` on its
empty-damage path at `0x140193ce6`); **1 = success**, 3 = running (per RE doc).

## Time-of-day source (issue #121)

The engine function `getGameTime()` (registered EngineFunction, impl
`0x1404691A0`) formats the game clock. Its value source is `0x140468760`,
which reads a **64-bit microsecond counter** and extracts H/M/S by
magic-number division (`/1e6 %60` = seconds, `/60e6 %60` = minutes,
`/3.6e9` = hours):

```
obj   = *(void**)(base + 0xB7E4C0)   // world/time singleton pointer
T     = *(int64*)((char*)obj + 0x10) // game time, microseconds
hour  = (T / 3'600'000'000) % 24     // hour of day, 0..23
```

`0x1402DA1C0` wraps the read with sentinel handling (±infinity/min/max
`0x8000000000000000`-family values) — irrelevant for a live clock, so we read
`obj+0x10` directly with a null guard on `obj`.

**Confirmed in-game (not wall-clock):** on `_c` the node read `hour=10` then
`hour=11` ~9 real-minutes apart, while the host wall-clock was 16:xx local /
14:xx UTC — neither value, and advancing faster than real time. So this is the
accelerated in-game day/night clock, which is what AI day/night logic wants.

XML attribute reads in a node's `loadFromXml` (slot 1) must use the engine's
**TinyXml1** attribute getter `0x45A920`
(`const char* __fastcall(TiXmlElement*, const char* name, int /*0*/)`), NOT
LiFx's tinyxml2 — the node receives a TiXml1 element. Confirmed: `_createNode`
calls it as `Attr(xml, "class", 0)`.

## AI context + movement (issue #123 — action nodes)

`getAiData()` (`0x1530D0`) just returns `*(void**)(node + 0x28)` — every node
carries a pointer to its creature's AI context (blackboard) at `+0x28`.

`GoToPosition::process` (`0x2E5C70`) is the canonical "path to a point" action
and the model for movement nodes:
- gets the creature from the blackboard, virtual-calls `getPosition`
  (`vtbl[0x280]`) → current pos.
- destination is stored **on the node**: x at `+0x40`, y at `+0x44`, z at
  `+0x48`; a "moving" flag byte at `+0x4c` (so a GoToPosition-shaped node is
  larger than the 0x48 base — do NOT store a 3-float vec on a 0x48 leaf).
- if `dist(creature, dest) <= arrivalRadius`: `MoveEngine_Stop` (`0x14014FE40`)
  + return `1` (success). Else `MoveEngine_SetTarget(engine, &node+0x40)`
  (`0x14014FD50`) + return `3` (running). The movement engine for a creature
  comes from `0x2E3380(creature)`.

**Creature lookup is class-keyed (the gotcha).** The creature isn't reached
directly — it's stored in the AI-context blackboard (a string→handle hash map)
under a key naming the creature class: **`"animal"`** for wild animals,
**`"npcbase"`** for human NPCs. `GoToPosition::process` is hard-wired to
`"npcbase"`, so reusing it verbatim fails on a grouse (`npc is nullptr`).
Lookup helper: `bbFind(aidata, &stdString)` (`0x190D70`, returns a creature
handle; `**handle` = creature). Build the `std::string` key with the engine's
String ctor `0x454FA0(&buf, "animal")` and destroy with `0x86D60(&buf)`.

**GoToPoint design (verified):** clone `GoToPosition` for its layout (dest at
`+0x40/+0x44/+0x48`, moving-flag at `+0x4c`), override slot 1 (loadFromXml,
parse `value="x y z"`) and slot 3 (own process). The process resolves the
creature via the blackboard trying `"animal"` then `"npcbase"` (so it works for
animals AND NPCs), gets the engine `0x2E3380(creature)`, and on the moving flag:
set target `0x14014FD50(engine, &dest)` / poll arrival `0x14014FD30(engine)==1`
/ stop `0x14014FE40(engine)`; returns 3 (moving) / 1 (arrived). The clone thunk
swaps the vtable; GoToPosition's clone copies the coord fields, no hand-copy.

## Implications for adding a node

- **Clones get the concrete class's hardcoded vtable**, so patching a
  prototype's vtable copy does NOT change ticked instances — slot 2 must be
  overridden so clones adopt our vtable.
- Object layout is class-specific, so a hand-rolled clone/dtor is risky. The
  safe, layout-agnostic technique used here: **delegate to the template
  class's own `clone`, then overwrite the result's vtable pointer with our
  patched copy** (slot 2 = our clone, slot 3 = our process; slots 0/1/4/5
  kept from the template so dtor/load/base methods stay correct for the
  template-shaped object).
- Registration trigger: hook the **per-tree XML loader** `0x153B80`
  (`void* __fastcall(self, const char* file)`, logs "Loading AI behaviour
  from [%s]"). The boot load path (`onServerCreated`) calls this directly —
  NOT `BehaviorsManager::reloadBehaviorXML` (`0x1506D0`), which is only the
  manual console reload (and itself calls `0x153B80` per tree). Registering
  on the first loader call (retried until built-ins are present) guarantees
  the custom node exists before any tree's nodes are parsed, on both boot and
  manual reload. Verified at runtime: registering on `0x1506D0` was too late
  (boot never calls it), so the first grouse load still errored; switching to
  `0x153B80` fixed it.
