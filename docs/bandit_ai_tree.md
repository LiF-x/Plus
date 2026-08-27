---
title: Bandit AI behaviour tree
status: design
domain: ai
tags: [ai, behavior-tree, npc, bandit, animal]
related: [ai_and_spawning.md, animal_spawn.md, bandit_combat.md, hostile-npc-ai-path-comparison.md]
sources: [dist/ai/cmAiBandit.xml, art/datablocks/animals/Wolf.cs]
updated: 2026-06-28
---

# Bandit AI behaviour tree

An aggressive humanoid hostile ("Bandit") on the **native `Animals::Animal`**
platform (Path 1): a Wolf-type animal (`BanditData : WolfData`, `animalTypeId 755`,
`shapeFile = male.dts`) — see [`animal_spawn.md`](animal_spawn.md) and
[`bandit_combat.md`](bandit_combat.md). Because the body is a real Animal it is
**vital-simulated** (moves, chases, takes damage, dies) and the **native animal AI
nodes** all work, so the tree is built on the engine's threat/aggression FSM
rather than custom `npcbase` nodes.

Tree file: [`dist/ai/cmAiBandit.xml`](../dist/ai/cmAiBandit.xml), derived from the
shipped `data/ai/cmAiWolf.xml`.

> History: an earlier version of this tree (#159) targeted the `NPCDecorative` /
> `npcbase` path. #154 re-platformed the bandit to an `Animals::Animal`, so the
> npcbase tree was orphaned and replaced by this native one (#161). The three
> custom-node issues (#155 HpBelow, #156 pathfind-flee, #157 MeleeSwing) were
> closed as superseded — the native suite provides those on an Animal.

## Behaviour

| Requirement | How it's met (native) |
|---|---|
| Aggressive — engage any player within 30m | aggression `EnemyInRange value="0 30"` + fast threat ramp → `SetAggressionState aggressive` at threat > 50 |
| Chase the player | `ChaseEnemy` (plays `Run`) when out of attack sector |
| Attack — left / right swing | `RandomSelector` over `Attack value="Fast"` (`hit1H_leftright_fire`) and `Attack value="Power"` (`hit1H_power_slash_fire`); damage via the #154 held-weapon + `endAttack` path |
| Flee at 10 HP (of 100) | `HpLessCheck value="10"` → `SetAggressionState frightened` → `Flee` |
| Target selection with multiple players | native threat system + `AimAtClosestEnemy` |

## Binding

`BanditData` inherits `WolfData`, whose datablock sets
`behavior = "data/ai/cmAiWolf.xml"`. To run this tree, set on **`BanditData`**:

```cs
behavior = "data/ai/cmAiBandit.xml";
```

`BanditData` is defined in the external HostileNPCs modpack / LiFx runtime (not in
this repo), so apply that one line wherever `BanditData` is declared. Deploy the
tree by copying `dist/ai/cmAiBandit.xml` to `<server>/data/ai/cmAiBandit.xml`
(server: `D:\Steam\steamapps\common\Life is Feudal Your Own\server`). Reload at
runtime with `reloadBehaviorXml();` then spawn a fresh bandit (`/animal BanditData`).

## Deltas vs `cmAiWolf.xml`

- **Aggression range** `15 30` → **`0 30`** (engage any player within 30m).
- **Become-aggressive threshold** `300` → **`50`** (aggressive, fast ramp); the
  alerted threshold drops accordingly.
- **male.dts-safe idle**: the wolf's `Idle_Sleep*` / `Idle_Eat` / `Idle_Stand` /
  `Threatened` animations have no `male.dts` remap (only `idle1`, `Run`,
  `Attack_Fast`, `Attack_Power` are confirmed — see `bandit_combat.md`), so idle
  uses `Move`/`Wait` only and the alerted branch just `AimAtClosestEnemy`.
- **Kept** from the proven wolf tree: the death handler, the full threat FSM, the
  flee-at-10%-HP "frightened" trigger, and the `Fast`/`Power` attack selection.

## Notes / open items

- The death handler keeps `PlayAnimation "Death"`; if `male.dts` lacks that
  sequence it resolve-misses (logged, non-fatal) and `Death` still runs. #154's
  `createCorpse` redirect handles the Player tombstone.
- Idle locomotion `Move value="0 15 40"` / `"1 15 40"` is the wolf's walk/run; the
  exact radius/cooldown tuning can be revisited once seen in-game.
