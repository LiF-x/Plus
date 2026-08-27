---
title: Hostile-NPC AI path comparison
status: design
domain: design
tags: [hostile-npc, npc-ai, behavior-tree, npcdecorative, worn-loot, reverse-engineering]
related: [ai_and_spawning.md, npc_class_hierarchy.md, animal_data_model.md, character_hp.md, lootstone_injection.md]
sources: [source/server/cm_offsets.h, source/server/api/lifx_hostile.cpp, source/server/hooks/ai/hook_behavior_node.cpp, source/server/hooks/ai/ABI_NOTES.md, source/server/hooks/ai/VERIFICATION.md, source/server/hooks/character/hook_vital_process_tick.cpp, HostileNPCs/src/modpack/mods/HostileNPCs/art/datablocks/Bandit.cs]
updated: 2026-06-26
---

# Hostile / Vendor NPC AI — Path Comparison Spec

**TL;DR.** Human-shaped hostile NPCs need a *body* (which class renders armor + drops worn
loot on death) and a *brain* (which behaviour-tree nodes detect/target/chase/flee/attack).
Two **bodies** exist on disk and trade off oppositely: the deployed `HostileNPCs/` mod folder
is an **off-spec Stage-0 `AnimalData` carcass** (combat works, but loot routes to animal
skinning, which is wrong), while the canonical **`NPCDecorative` Player-tombstone** path
(issue #125, `lifxpluss/source/server/api/lifx_hostile.cpp`) binds a real character so death
rides the engine's own Player corpse→worn-loot pipelines. **#125 supersedes the carcass; the
user chose to re-platform onto the tombstone.** For the **brain**, the native combat nodes are
hard-gated to `Animals::Animal`, so the chosen plan keeps the `NPCDecorative` body and ports
the animal node suite to gate-less `"npcbase"` nodes (Path 2 below) — re-platforming the body
onto `Animals::Animal` (Path 1) would forfeit the tombstone. The one finding that complicates
this: **`NPCDecorative` is not in the server vital/damage simulation**, so killability is a
real Path-2 integration cost (§6.3), not the "shared free-for-all spike" the original §3.4 framing
implied.

_Status: design decision pending. Authored 2026-06-21 from RTTI + disassembly RE of
`lif_server_320850_d/ddctd_cm_yo_server.exe` (ImageBase `0x140000000`). Companion to the
A2a RE notes (`lifxpluss/source/server/hooks/ai/ABI_NOTES.md`) and the shipped custom-node
work (PR #120/#122/#124). Merged 2026-06-26 with the two-threads / vital-sim findings (§6)._

## 0. The decision in one line

We want human NPCs (vendor, guard, bandit) that detect, target, face, flee, chase and
attack. The native animal AI nodes that do this are **hard-gated to `Animals::Animal`** — so
the choice is **(Path 1)** re-platform the NPC onto an `Animals::Animal` subclass and inherit
the whole native suite, vs **(Path 2)** keep our shipped `NPCDecorative` and write a small set
of custom `"npcbase"`-keyed nodes that drive the same movement engine.

## 1. The RE finding that frames everything

RTTI base-class arrays give the true hierarchy:

```
Player   (carries CharacterParameters / CharacterCriminals / IDamagableCreature)
 └─ NPCS::Base          ctor 0x2E2C10 — writes BOTH charStats AND the AI layout
     │                  (+0x24B8 AI-tree, +0x24C0 move-engine, +0x24CC/+0x24D0 range floats 50/100)
     ├─ NPCS::AnimatedNPC      ctor 0x2E2390   (adds only +0x24E0=-1, +0x24E8=0; obj 0x24F0)
     │    └─ Animals::Animal                    (adds the perception block: +0x2500/+0x2520/+0x2528)
     └─ NPCS::PlayerBased
          └─ NPCS::NPCDecorative  ctor 0x2E46B0 (obj 0x2518)  ← what HostileNPCs spawns today
```

Two facts kill the originally-proposed options:

1. **"Spec the AnimatedNPC interface for NPCDecorative" has nothing to port.** Both primary
   vtables are 196 slots. AnimatedNPC overrides 11 `Base` slots; NPCDecorative overrides a
   strict superset (those 11 + 7 more). The "AnimatedNPC-only" override set is **empty**. The
   AI-tree/move-engine machinery is set by the **common `NPCS::Base` ctor**, so NPCDecorative
   already has it — which is why our `GoToPoint`/patrol nodes already work on it.

2. **The combat nodes gate on `Animals::Animal`, not AnimatedNPC.** `EnemyInRange::process`
   (`0x1941A0`), `HasTarget` (`0x194830`), `SetPlayerAsTarget` (`0x18FAB0`), `ResetTarget`
   (`0x18FE60`), `ThreatValueChange` (`0x190350`) all resolve the blackboard via the **`"animal"`
   key** (`SimObjectPtr<Animals::Animal>` find `0x190D70`) and touch **Animal-only offsets**:
   enemy-list `+0x2500`, current-target `+0x2520`, threat `+0x2528`. On a non-Animal the resolve
   returns null → the exact in-game crash `AI::Nodes::EnemyInRange::process() - object is nullptr`.
   There is **no `NPCS::Base`-level target slot** and **no engine perception pass** that populates
   a target for a non-Animal — acquisition is done *by the Animal-keyed nodes themselves*. The one
   exception, `AimAtClosestEnemy` (`0x193DA0`), is keyed `"npcbase"` and runs on any NPC, but only
   orients (`[npcbase_vtbl+0x618]`); it acquires nothing.

**Upside that survives either path:** AnimatedNPC and Animal are *both* `Player`-derived, so they
carry the same charStats/equip/`CharacterCriminals`/damageable layout. Going to an Animal does
**not** inherently forfeit equip-render or loot, and criminal-tag detection is reachable from any
of these classes.

---

## 2. Path 2 — custom `npcbase` nodes on `NPCDecorative` (recommended)

Keep the shipped `NPCDecorative` (autonomous movement, equip-over-ghost render, worn-loot,
reconnect persistence — all in-game-verified, PR #126). Reimplement only the perception/target
nodes we need, as gate-less custom LiFx nodes built like `GoToPoint`: resolve the creature via the
`"npcbase"` key (`SimObjectPtr<NPCS::Base>` find `0x190E90`), keep target/threat/cooldown state in a
**creature-keyed side table** (the `WalkWaypoints` side-table pattern from the caravan design), and
drive the **same `NPCS::Base` move-engine at `+0x24C0`** the animal nodes use (`getEngine 0x2E3380`;
`setTarget 0x14014FD50`, arrival poll `0x14014FD30`, stop `0x14014FE40`).

### 2.1 Node inventory

| Node | Type | Resolver | Reads / writes | Engine primitives | Effort |
|---|---|---|---|---|---|
| `SetNearestPlayerAsTarget value="radius [tag]"` | action | npcbase `0x190E90` | scans players in `radius`, optional `criminal` filter; writes chosen player into side-table target slot | `initContainerRadiusSearch` + `containerSearchNext` (exposed), RTTI/`getControlObject` filter to Players, `CharacterCriminals` flag read | **M** — core node; scan + filter |
| `TargetInRange value="min max"` | condition | npcbase | reads side-table target, distance vs self `getPosition` (`vtbl+0x280`) | none | **S** |
| `HasPlayerTarget` | condition | npcbase | side-table target non-null + still valid (`isObject`) | none | **S** |
| `FaceTarget` | action | npcbase | orient toward side-table target | reuse orient virtual `[npcbase_vtbl+0x618]` (the one `AimAtClosestEnemy` calls) | **S** |
| `ChaseTarget` | action | npcbase | move-engine setTarget toward target's live pos each tick | `0x2E3380`/`0x14014FD50`/`0x14014FD30` (GoToPoint-shaped, dynamic dest) | **S–M** |
| `FleeFromTarget` | action | npcbase | move-engine setTarget to a point *away* from target (reflect vector) | same move-engine | **M** |
| `CooldownGate value="name secs"` | condition | npcbase | side-table per-name timer (in-game clock `*(*0x140B7E4C0)+0x10`, already used by `TimeOfDayBetween`) | none | **S** |
| `ReturnToHomePosition` | action | npcbase | home = `homePos` dynfield (`SimObject::getDataField`) else first-tick capture in side table; fails when already home | move-engine | **S–M** |
| `AttackTarget` | action | npcbase | initiate a melee swing against side-table target | **RE SPIKE** — Player swing-initiator RVA not yet pinned (`Player::_applyHit 0x0EE0F0` is the *landed-hit* side, not the initiator) | **L (spike)** |

### 2.2 Shared infrastructure (build once)

- **Creature-keyed side table** — `std::unordered_map<SimObjectId, NpcAiState>` holding
  `{target SimObjectPtr, threat, cooldown timers, homePos}`. Keyed by `SimObjectId` (pointer-reuse
  safe), cleared on NPC removal. ~½ day. This is the same structure the caravan `WalkWaypoints`
  node needs, so it is shared investment, not throwaway.
- **Player-scan helper** — wrap `initContainerRadiusSearch(point, radius, PlayerObjectType)` +
  `containerSearchNext` loop; filter to `Player`-derived; optional `CharacterCriminals` predicate.
- Registration: same `0x153B80` per-tree-loader hook already used for `GoToPoint`
  (`CmOffset::AI_LOAD_BEHAVIOR_XML`).

### 2.3 Open RE spikes (firm up before committing the estimate)

1. **Attack initiation** (the big one) — find the server-side melee swing initiator for a
   `Player`/`NPCS::Base` so `AttackTarget` can deal damage. Combat "can't kill / can't hit" is the
   long-standing open gate from the A2a handoff; affects Path 1 too (see §3.4). **Refined 2026-06-24
   (#144): the shipped `AttackTarget` calls `useAbility(0x9AC20, abilityId)`, which targets the
   _crafting_ ability system (Gather/Create/Saw), NOT melee — so it does not deal combat damage.
   Real swings live in `cm_special_attacks.xml` (event ids) / `attack_animations.xml` and require an
   equipped weapon. See §6.4.**
2. **`CharacterCriminals` flag** — exact offset/getter for "is this player flagged criminal" (the
   class is in the base chain at `CharacterCriminals@CharacterStatsAPI`; needs the read pinned).
3. **`AimAtClosestEnemy` target source** — confirm whether reusing `[npcbase_vtbl+0x618]` directly
   needs a target argument or self-reads a slot (decides if `FaceTarget` is trivial reuse).

### 2.4 Effort / risk / what's preserved

- **Effort:** ~3–5 dev-days for the movement/perception/flee/return/cooldown set (most are S),
  **plus** the `AttackTarget` spike (unknown until combat-init RVA is found — could be 1 day or a
  week). Vendor + guard (no attack needed for vendor; guard warn-then-attack needs attack) and the
  bandit's non-combat behaviors are reachable **without** solving attack.
- **Risk:** low for everything except attack. Built entirely on proven mechanisms (custom-node ABI,
  move-engine, container search). No re-platform.
- **Preserved:** 100% of shipped `NPCDecorative` work — equip-over-ghost render, per-type loadouts,
  worn-loot/death pipeline, reconnect persistence, autonomous movement, patrol.

---

## 3. Path 1 — re-platform onto an `Animals::Animal` derivative

Spawn the NPC as an `Animals::Animal` subclass with a humanoid datablock so the **entire native
27-node animal AI suite** resolves (the `"animal"` blackboard key is satisfied, the `+0x2500`
perception block exists).

### 3.1 What you inherit for free

`EnemyInRange`, `EnemyInAttackSector`, `EnemyOutOfReach`, `HasTarget`, `DistanceToTargetLess`,
`AggressionStateCheck`/`SetAggressionState`, `ThreatValueGreater`/`ThreatValueChange`,
`SetPlayerAsTarget`, `ResetTarget`, `ChaseEnemy`(+NoAnimation), `Attack`, `Flee`,
`AimAtClosestEnemy`, `ClearEnemyInteractions`, `Death`, plus the aggression FSM and threat
accumulator — all working with no new C++. This is the headline advantage: a *rich* combat brain
authored entirely in behavior-tree XML.

### 3.2 Re-platform work items

| Item | Work | Risk |
|---|---|---|
| **Register an Animal-derived type with a humanoid datablock** | New `AnimalData`-backed type (children of 751 per the animal data model), shape = `male.dts`. Spawn via the Animal manager / `cm_spawn_patterns.xml` rather than our maintenance tick. | M — Animal spawn lifecycle differs from our NPCDecorative respawn tick |
| **Humanoid render as an Animal** | Verify `male.dts` renders + animates under the Animal render path (animals normally use animal shapes). Animation uses `AnimatedNPC::setAnimation 0x2E2A90` — present on Animal — but human anim sets / locomotion blending unverified on an Animal. | **H — unverified**; the whole point of NPCDecorative was a player-model that renders armor |
| **Re-gate equip-over-ghost** | The packUpdate/unpackUpdate vtable-slot patch is gated to the **NPCDecorative vtable** (server slot 54, client slot 56). Re-point to the `Animals::Animal` vtable + re-verify the bit-symmetry handshake. | M — mechanism proven, just re-targeted |
| **Death → worn-loot** | See §3.4 — Animal death likely routes to **skinning/loot-recipe** (animal carcass), **not** the Player corpse→worn-equipment drop. This is the biggest semantic divergence. | **H** |
| **Criminal detection** | Still custom — the native animal nodes detect threats via the aggression/threat system, not a criminal tag. A criminal-aware target filter is custom on either path. | — (equal to Path 2) |
| **Prohibited-land / wild-animal behaviors** | Ensure inherited animal behaviors (e.g. `IsStandingOnProhibitedLand`, spawn-pattern culling) don't fire inappropriately on a town vendor/guard. | L–M |

### 3.3 Effort / risk / what's at stake

- **Effort:** the *AI* is cheap (XML), but the re-platform is ~1–2 weeks dominated by re-verifying
  render/animation/equip/loot on a class we haven't used for player-models, plus reworking spawn
  lifecycle. Throws the verified NPCDecorative path into "re-prove" status.
- **Risk:** high and front-loaded on the two unverified H items (humanoid render-as-Animal, worn-loot
  on Animal death).

### 3.4 The death/loot divergence (decisive sub-question, affects both paths)

`NPCDecorative` reuses **Player** death/corpse/worn-loot vtable slots (`spawnLootstone 0x102570`,
`onDeathHappens 0xFB390`) — that's *why* it was chosen. `Animals::Animal` is also Player-derived so
those slots exist, **but** its own death override may route to the animal skinning/loot path instead.
Whichever path we pick, **combat killability + worn-loot drop is the same unsolved gate** (the A2a
handoff's "make it killable" item). Recommend a short RE spike on Animal's `Death`/`onDeathHappens`
override before betting Path 1 on inherited loot.

> **UPDATE 2026-06-24 (in-game on `_d`) — this "shared gate" framing is now refined, not equal.**
> `NPCDecorative` is **not in the server vital/damage simulation** (proof + consequences in §6.3),
> so on Path 2 the NPC's HP is frozen and it cannot be damaged or die without wiring it into the
> vital sim — a real integration cost, not a free shared spike. `Animals::Animal` **is**
> vital-simulated (killable + damageable for free); its only loss is that its death routes to the
> animal carcass/skinning path and would need redirecting to the Player `spawnLootstone 0x102570`
> tombstone. This briefly tipped the analysis toward Path 1; the user re-decided to stay on Path 2
> and wire the vital sim (§6.2). Either way, `spawnLootstone 0x102570` / `onDeathHappens 0xFB390`
> are the loot/death slots in play.

---

## 4. Side-by-side

| Dimension | Path 2 (custom npcbase nodes) | Path 1 (Animal derivative) |
|---|---|---|
| Native combat suite | ✗ reimplement the few we use | ✓ all 27 nodes free (XML only) |
| Preserves shipped NPCDecorative work | ✓ untouched | ✗ re-verify render/equip/loot |
| Humanoid render + armor | ✓ proven | ✗ unverified on Animal |
| Worn-loot on death | ✓ Player path intact | ⚠ likely skinning path — needs spike |
| New C++ | ~6 small nodes + side table | ~0 nodes; ~big integration |
| Movement | ✓ GoToPoint proven, same engine | ✓ native |
| Attack/killable | ✗ open spike (see §6.3: Path 2 also needs vital-sim wiring) | ✓ vital-simulated (killable for free); attack-init still open |
| Criminal-tag flee | custom node (equal) | custom filter (equal) |
| Effort | ~3–5 days + attack spike + vital-sim wiring | ~1–2 weeks + 2 high-risk spikes |
| Risk profile | low, incremental | high, front-loaded |

## 5. Recommendation

**Path 2.** It builds on a working, in-game-verified foundation, the custom-node mechanism is
already proven three times over, and the only genuinely hard problem (attack initiation / killable)
is **shared by both paths** — so Path 1's main selling point (free combat nodes) doesn't actually
clear the hardest gate, while it does put the verified render/equip/loot work back into question.
Reserve Path 1 for the scenario where we want a *deep* native aggression/threat brain and are
willing to re-platform; if so, run the §3.4 death/loot spike and the humanoid-render-as-Animal spike
first, because those two H-risk unknowns decide whether Path 1 is even viable.

**Shared prerequisite regardless of path:** the combat-init / killable RE spike. Worth doing next
either way.

> **Decision recorded 2026-06-24:** the user chose **Path 2 (stay `NPCDecorative`)** for the brain
> **and** the **`NPCDecorative` Player-tombstone body** (#125) over the deployed carcass — and
> accepted the vital-sim wiring cost (§6.3) that Path 2 carries for killability. Re-platforming the
> body onto `Animals::Animal` would forfeit the tombstone/worn-loot pipeline, so it is off the table.

---

## 6. The two threads + implementation state (#125 / #144) — merged 2026-06-26

This section integrates the post-spec RE and the decision history. Everything above (§0–§5) is the
original 2026-06-21 comparison and is preserved; this section reconciles it with what was found
afterward.

### 6.1 Two divergent bodies on disk (the "why are we back to carcass?" confusion)

The hostile-NPC work split into **two implementations that drifted apart**, which is the source of
the confusion. They are not two *plans* — they are two *bodies* that already exist:

| | **Deployed `HostileNPCs/` mod folder** | **Canonical #125 — `lifxpluss/.../lifx_hostile.cpp`** |
|---|---|---|
| Body class | `datablock AnimalData(BanditData : WolfData)`, `shapeFile = "art/models/3d/mobiles/characters/male.dts"` | `NPCS::NPCDecorative` with a **bound character** |
| Spawn | `spawnObject("Wolf", "BanditData")` (animalTypeId 755 = Wolf), wild via `cm_spawn_patterns.xml` | `Lifx::makePlayerNpc(<charID>)` / `Lifx::spawnHostile(...)` |
| Death / loot | inherits WolfData's `rawCorpseObjectTypeID`/`skinnedCorpseObjectTypeID` → **animal carcass + skinning loot** (WRONG) | death → tombstone → **worn-loot via the engine's Player pipeline** (`spawnLootstone 0x102570`, `onDeathHappens 0xFB390`) (CORRECT) |
| Combat | real HP damage works (Animal is vital-simulated) | NPCDecorative needs vital-sim wiring (§6.3) |
| Equip render | shows all-armor | resolves via bound character's `CmPlayerEquipment` |
| Status | **Stage 0, OFF-SPEC** | **canonical; #125 explicitly supersedes the Stage-0 AnimalData bandit** |

The `: WolfData` inheritance in the carcass build was a **crash workaround** — inheriting
`DefaultPlayerData` directly crashed datablock preload, so it piggybacks on the already-loaded
WolfData and overrides only `shapeFile`. The side effect is that it also inherits WolfData's corpse
type IDs, which is what makes the body skinnable instead of a tombstone.

> **Stale-docs warning:** the `HostileNPCs/` `README.md` / `docs/PLAN.md` / older comparison notes
> still describe the **"skinnable corpse"** as if it were intended. Those read like a regression and
> are **superseded** — the worn-loot tombstone (#125) is the spec. Do not treat the carcass loot as
> a feature.

### 6.2 User decisions (2026-06-24)

1. **Re-platform to the Player tombstone path** — retire the carcass Stage 0, finish #125.
2. **For the AI brain, stay on `NPCDecorative`** and port the full vanilla animal-node suite to
   **gate-less `"npcbase"` nodes** (Path 2). Do **NOT** re-platform onto `Animals::Animal` — that
   would break the tombstone.
3. Later the same day, after the vital-sim finding (§6.3) briefly favored switching the body to
   `Animals::Animal`, the user **re-confirmed staying on `NPCDecorative`** and **wiring it into the
   vital/damage sim** instead.

Work happens on branch `feat/a2a-character-backed-hostile` (carries all #125 work + `lifx_hostile.cpp`).
The #135 chase/flee/face/attack nodes live on `feat/npc-chase-flee-face` and must be merged in. Plan
doc: `npcbase-node-suite-plan.md`. Test assets: `aiNodeTest.xml` (the 9 nodes currently on the
branch) and `aiNpcCombatTest.xml`.

### 6.3 DECISIVE FINDING — `NPCDecorative` is **not** in the vital/damage simulation

In-game on `_d`: spawned an `NPC_player_male` (obj `…A5C0`, charStats `…B068`); `lifxBindNpc(1)`
succeeded and **equipment resolved non-null** (bind→equipment works). But `Lifx::dumpCharStats()`
showed **only one** registry entry — the real player (charStats `…8A68`, hardHp `105/105`). The
NPC's charStats `…B068` was **absent from the registry**, so `VitalParams::ProcessTick` never fires
for it. Consequences:

- Its HP is **frozen** — born at ~`2/800`, never initialized, never decremented. Sword hits do not
  reduce server HP ("registers on client" is purely cosmetic); it **cannot die**.
- The #144(1) "tick the AI tree from `VitalParams::ProcessTick`" fix is a **no-op for this NPC** —
  ProcessTick doesn't fire for it; chase is purely `setActive`.
- Making an `NPCDecorative` killable→tombstone requires **wiring it into the vital/damage sim** =
  major RE, **not** a small HP-init.

By contrast, `Animals::Animal` is vital-simulated (killable + damageable for free). This is why the
analysis briefly favored Path 1; the user instead chose to wire `NPCDecorative` into the vital sim
(§6.2 item 3). NPC→player damage is unaffected because the **player** is simulated.

### 6.4 NPCDecorative AI tick + the heartbeat work (#144)

How animals tick vs. how NPCDecorative doesn't:

- Animals tick their tree via `Animals::Animal::packUpdate` (`0x18B450`) → `AiTree::process`
  (`0x154020`, `CmOffset::AI_TREE_PROCESS`), which **also binds the per-node AI-context** (blackboard
  ptr at `node+0x28`, via the `0x454fa0`/`0x457xxx` setup) before each tick.
- `NPCDecorative::packUpdate` does **NOT** call it — slaves stay static. Its own `packUpdate` thunk
  is `0x2E54C0` (`CmOffset::NPCDEC_PACKUPDATE_THUNK`, slot 54), reaching the shared `ShapeBase`
  pack at `0x0FC8B0`.
- A **bare `AiTree::process` call crashes** without that context binding — confirmed twice. The
  per-node AI-context binding is provided **only** by the engine's own AI-tick path. So a manual
  tree tick from any other thread/context crashes.

What was tried (heartbeat experiments, `NpcDecPack::HeartbeatTick`, gated by `lifxAiTick` /
`g_aiNpcAny`, vtable-guarded):

- (a) Driving a heartbeat from the **connected player's** `VitalParams::ProcessTick` (the only
  per-frame tick that fires) is **confirmed firing** (`[lifx-hb]` log: `vtblOK=1, n=1`).
- (b) `_VitalParams_ProcessTick(npcCharStats)` from the heartbeat is **safe** (300+ calls) but
  **ineffective** — HP stayed frozen `2/800`. Vital-ticking alone does not initialize/animate HP;
  the HP fields were never initialized (need current HP = max).
- (c) `_AiTree_Process(npcTree)` from the heartbeat **crashes** the instant a tree is attached
  (missing AI-context binding, §6.4 above) — **removed** from the heartbeat. Stable build =
  heartbeat does vital tick + pos logging only (md5 `c3ca0a9a`).
- (d) Gotcha: `setBehavior(name)` attaches **nothing** unless `registerBehavior(name,path)` +
  `reloadBehaviorXml()` ran **first**; `[lifx-hb] tree=0` means no tree attached.
- (e) `lifxBindNpc` guards against double-bind (charStats vtbl already cloned → ABORT).
- (f) Spawn cmds (`makePlayerNpc` / `spawnNpcAtChar` / `spawnNpcDecorative`) **return the object id**
  via `StringCallback` → `$npc = Lifx::makePlayerNpc(1);` (use `$global`, not `%local`).
- (g) **Movement + replication work:** with a tree attached (register → `setBehavior` →
  `setActive`, `lifxAiTick` LAST) the move engine is advanced every frame by `setActive`'s path and
  the position **replicates** — the NPC drifts toward the player's set-once position (`[lifx-hb]
  pos=` changed ~33 units / 48s).
- (h) **But** the AI tree only ticks at **setup** (~5 ticks via `setActive`'s path, with a valid
  AI-context), **not per-frame** → it sets ONE chase destination and walks there, never re-tracking.

The fix (#144, on branch) drives `AiTree::process` from `VitalParams::ProcessTick` at full tick
cadence for AI-flagged NPCs (gated by `NpcDecPack::IsAiNpc`, atomic short-circuit + vtable guard);
in `ProcessTick`, `self` **is** charStats `== creature+0xAA8` (charID at `+0x109C` confirms), tree at
`creature+0x24B8` (`CmOffset::NPC_AI_TREE_OFF`). It still requires `setActive(1)` first for the
binding — and §6.3 shows ProcessTick doesn't even fire for an unregistered NPCDecorative, so the
vital-sim wiring is a prerequisite for this fix to do anything.

**Two routes to continuous AI (no tree tick required for the cheap one):**

- (A) **Safe workaround** — drive the move-engine `setTarget` directly from the heartbeat toward the
  live player pos: `getEngine 0x2E3380(creature)` → `setTarget 0x14FD50` (absolute `0x14014FD50`).
  These calls do **not** need the blackboard binding (they are what `GoToPoint` uses) → continuous
  chase **without** ticking the tree. Pursued first for a visible chase.
- (B) **Proper** — hook the engine's per-frame NPC tick (where the move engine is advanced and the
  AI-context is valid) and tick the tree there.

**Correct activation sequence (console):**
`makePlayerNpc(<cid>)` (defines `NPC_player_male` — **NOT** `spawnNpcAtChar`, which defaults to
`NPC_slave_A`) → `.lifxGhost()` → `.lifxLoadout(0=plate | 1=leather)` → `.setBehavior("tree")` →
`.setActive(1)` → `.lifxAiTick()`. HP readout: `%npc.lifxHp()` (#144) via
`Engine::HardHpDisplay` / `Engine::SoftHpDisplay` on charStats.

### 6.5 Combat-damage gap (#144(3)) — the long pole

The shipped `AttackTarget` calls `useAbility(0x9AC20, abilityId)`, but `0x9AC20` is the engine's
**crafting** ability trigger — the ids in the abilities table are Gather/Create/Saw (crafting), **not
melee**. So `AttackTarget` does not deal combat damage. Real swings live in `cm_special_attacks.xml`
(event ids) + `attack_animations.xml` and **require an equipped weapon**. Lethal combat therefore
needs the special-attack/weapon path RE'd **plus** the #125 weapon-equip — the long pole.
(`Player::_applyHit 0x0EE0F0` = `CmOffset::PLAYER_APPLY_HIT` is the **landed-hit** side only, not the
initiator.) Note: NPC→player damage (the #144(3) capture-replay `AttackTarget`, built but not
deployed) still works regardless, since the player is vital-simulated.

### 6.6 Open items on #125 (the real remaining work)

1. **Character-bind completion (THE gate).** `bindNpc` exists — it clones the charStats vtbl, sets
   the character-backed flag `charStats+0x4A9 = 1`, and flushes the equip cache (`+0x498` cached
   `CmPlayerEquipment*`, `+0x4A0` cached refcount) — but a fresh charID "has no `CmPlayerEquipment`
   yet; render needs an equipped source character." The Phase-0 probe (`dumpNpcEquip`) must confirm
   the NPC charStats yields a **non-null `CmPlayerEquipment`** via the registry accessor `0x28BD30`
   (`CmOffset::EQUIP_ACCESSOR`), which returns a record whose `[+0x48]` is the `CmPlayerEquipment*`.
   Non-null → render + worn-loot are **free**; null → seed one hash-map entry. (**Correction per
   `cm_offsets.h`:** `EQUIP_ACCESSOR`'s arg1 is the global type/registry singleton `*0x140B53908`,
   **not** charStats, and the lookup key comes from a `vtbl[1]` virtual call on the entity — the
   earlier `charStats+0xB14` key reading crashed / was found wrong and is removed; the exact
   handle→`CmPlayerEquipment` chain still needs a clean static RE pass.) (Field-offset corroboration in
   `lifx_hostile.cpp`: `kCharBackedFlagOff=0x4A9`, `kEquipCacheOff=0x498`, `kEquipCacheRefcOff=0x4A0`,
   `kCharIdFieldOff=0x109C`.)
2. **`Lifx::spawnHostile(x,y,z,loadout)`** full chain: create `0x2E4AB0`
   (`CmOffset::NPCDECORATIVE_CREATE`) → bind `CreateTestCharacter 0x1D29B0`
   (`CmOffset::CREATE_TEST_CHARACTER`) → equip slots → `setBehavior 0x2E4850`
   (`CmOffset::SET_BEHAVIOR`). **Death/loot needs NO new code once the bind holds.**
3. **Combat damage (Phase 2)** — AI nodes chase/flee/face/`AttackTarget` merged (#133/#134/#135), but
   the player swing-initiator RVA was "not yet pinned" in #125, and #144 showed `AttackTarget`'s
   `useAbility` is the wrong (crafting) system (§6.5). Verify whether #135 `AttackTarget` actually
   deals damage or merely initiates.

---

## Offset cross-reference (verified against `cm_offsets.h`)

| Symbol / role | RVA | `CmOffset` constant | Notes |
|---|---|---|---|
| `NPCS::Base` ctor — writes charStats `+0xAA8`, tree `+0x24B8`, engine `+0x24C0` | `0x2E2C10` | `NPCS_BASE_CTOR` | |
| `NPCS::NPCDecorative` ctor (obj size `0x2518`) | `0x2E46B0` | `NPCDECORATIVE_CTOR` | |
| `NPCDecorative` create (malloc+ctor+registerObject) | `0x2E4AB0` | `NPCDECORATIVE_CREATE` | |
| `NPCS::AnimatedNPC` ctor | `0x2E2390` | — | not in `cm_offsets.h` |
| `setBehavior(creature, treeName)` | `0x2E4850` | `SET_BEHAVIOR` | |
| `TREE_ATTACH(creature, **tree)` → installs at `+0x24B8` | `0x2E38D0` | `TREE_ATTACH` | |
| `AiTree::process` (per-node ctx bound by Animal pack) | `0x154020` | `AI_TREE_PROCESS` | |
| `Animals::Animal::packUpdate` (ticks the tree) | `0x18B450` | — | comment only |
| AI tree offset `creature+0x24B8` | `0x24B8` | `NPC_AI_TREE_OFF` | |
| move-engine offset `creature+0x24C0` | `0x24C0` | `NPC_MOVE_ENGINE_OFF` | |
| `getEngine(creature)` | `0x2E3380` | — | raw `AtRva` in `hook_behavior_node.cpp` |
| move-engine `setTarget` | `0x14FD50` (abs `0x14014FD50`) | — | GoToPoint primitive |
| move-engine arrival poll | `0x14FD30` (abs `0x14014FD30`) | — | |
| move-engine stop | `0x14FE40` (abs `0x14014FE40`) | — | |
| per-tree-loader registration hook | `0x153B80` | `AI_LOAD_BEHAVIOR_XML` | |
| `AnimatedNPC::setAnimation` | `0x2E2A90` | `ANIMATED_NPC_SET_ANIMATION` | |
| `spawnLootstone` (Player vtbl slot 44) | `0x102570` | `SPAWN_LOOTSTONE` | corpse container |
| `onDeathHappens` (Player vtbl slot 48) | `0x0FB390` | `ON_DEATH_HAPPENS` | death orchestrator |
| `Player::_applyHit` (landed-hit side) | `0x0EE0F0` | `PLAYER_APPLY_HIT` | NOT the swing initiator |
| `useAbility(charStats, abilityId)` — **crafting**, not melee | `0x9AC20` | — | raw in `hook_behavior_node.cpp` |
| equip registry accessor (registry global `*0x140B53908`; `CmPlayerEquipment*` = `[record+0x48]`) | `0x28BD30` | `EQUIP_ACCESSOR` | per `cm_offsets.h`: arg1 is the registry global, **not** charStats; the `charStats+0xB14` key was found wrong |
| `CreateTestCharacter(accountId, charId)` | `0x1D29B0` | `CREATE_TEST_CHARACTER` | full DB-side char |
| `NPCDecorative` packUpdate thunk (slot 54) | `0x2E54C0` | `NPCDEC_PACKUPDATE_THUNK` | shared pack `0x0FC8B0` |
| shared `ShapeBase`/`Player` packUpdate | `0x0FC8B0` | `SHAPEBASE_PACKUPDATE` | |
| AI node — `EnemyInRange::process` (gated `"animal"`) | `0x1941A0` | — | |
| AI node — `HasTarget` | `0x194830` | — | |
| AI node — `SetPlayerAsTarget` | `0x18FAB0` | — | |
| AI node — `ResetTarget` | `0x18FE60` | — | |
| AI node — `ThreatValueChange` | `0x190350` | — | |
| AI node — `AimAtClosestEnemy` (keyed `"npcbase"`, orient only) | `0x193DA0` | — | `[npcbase_vtbl+0x618]` |
| blackboard resolve — `SimObjectPtr<Animals::Animal>` find | `0x190D70` | — | `"animal"` key |
| blackboard resolve — `SimObjectPtr<NPCS::Base>` find | `0x190E90` | — | `"npcbase"` key |
| `Animals::Manager::createAnimal` (#145 Animal path) | `0x195FD0` | `CREATE_ANIMAL` | |

Charstats field offsets used by the bind path (corroborated in `lifx_hostile.cpp`): char-backed flag
`+0x4A9`, equip cache `+0x498`, equip cache refcount `+0x4A0`, engine charID field `+0x109C`, charStats
`creature+0xAA8` (the `+0xB14` equip-registry-key reading was found wrong — see the `EQUIP_ACCESSOR`
note above). Animal perception block (Animal-only): enemy-list
`+0x2500`, current-target `+0x2520`, threat `+0x2528`.

---

## Status & provenance

**Runtime-verified (in-game on `_d`):**
- `NPCDecorative` is **not** in the server vital/damage simulation (§6.3) — its charStats are absent
  from the `dumpCharStats` registry, HP frozen, not killable.
- `lifxBindNpc` succeeds and **equipment resolves non-null** after bind (§6.6 item 1 is reachable).
- Movement + position **replication** work via `setActive`'s path; the tree ticks only at setup
  (~5 ticks), not per-frame (§6.4 g/h).
- The heartbeat from the player's `VitalParams::ProcessTick` fires; calling `_AiTree_Process` from it
  **crashes** (missing per-node AI-context binding) — confirmed twice.
- The deployed `HostileNPCs/` carcass: real HP damage works, equip shows all-armor, loot is the
  (wrong) animal skinning path.

**Reverse-engineered (disassembly / RTTI, not all runtime-verified):**
- The class hierarchy and vtable-override analysis (§1).
- The Animal-gated AI node RVAs and their `"animal"`/`"npcbase"` blackboard keys (§1.2).
- `AttackTarget`'s `useAbility 0x9AC20` being the crafting system (§6.5) — RE-confirmed; the real
  special-attack/weapon path is **not** yet RE'd.
- All RVAs in the cross-reference table; those carrying a `CmOffset` name are present in
  `cm_offsets.h`. The move-engine `setTarget`/poll/stop, `getEngine 0x2E3380`, `useAbility 0x9AC20`,
  the AI-node RVAs, the blackboard-find RVAs, and `Animals::Animal::packUpdate 0x18B450` are used
  raw in source (or appear only in comments) and have **no** named constant.

**Design / proposal (not built or not yet decided):**
- Path 2 node inventory and effort estimates (§2), the Path 1 re-platform plan (§3).
- `Lifx::spawnHostile(x,y,z,loadout)` (§6.6 item 2) — not yet implemented.
- Vital-sim wiring for `NPCDecorative` to make it killable→tombstone — the open major-RE item the
  chosen path depends on.

**Decision of record (2026-06-24):** stay on the `NPCDecorative` Player-tombstone body (#125,
supersedes the carcass Stage 0) with Path-2 `"npcbase"` AI nodes; wire `NPCDecorative` into the
vital/damage sim. Branch `feat/a2a-character-backed-hostile`; #135 nodes to be merged from
`feat/npc-chase-flee-face`.
