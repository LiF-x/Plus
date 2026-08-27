# HostileNPCs — player-model hostile bandit (Animal platform)

The #154 hostile **bandit**: an `Animals::Animal` (animal type **755**, inherits `WolfData`)
re-skinned to the **player male body** (`male.dts`), with an aggressive AI tree, a real
held weapon, server-side melee damage, and a stamina-paced, rooted attack. This is the
**canonical Animal-platform bandit** — the older `NPCDecorative` path (`dist/ai/cmAiBandit.xml`)
is abandoned.

## Files

| File | Side | Purpose |
|---|---|---|
| `mod.cs` | server | Registers `BanditData` (on `onServerCreated`) + `NPC_player_male`; GM `serverCmdSpawnBandit`. |
| `cmod.cs` | client | Registers `BanditData` on the client so the bandit renders. |
| `art/datablocks/Bandit.cs` | both | `datablock AnimalData(BanditData : WolfData)` — id `250`, `shapeFile=male.dts`, `maxHP=120`, `behavior="data/ai/aiBanditAggressive.xml"`. Inherits the wolf's weapon/hit-trace/corpse/speeds. |
| `data/ai/aiBanditAggressive.xml` | server | The AI behaviour tree (copied from `cmAiWolf.xml`). Engages fast (threat threshold 50) and uses a **committed, rooted attack** (see below). |

## Requires the LiFx DLL

The mod is **data only**; the combat behaviour lives in the LiFx server DLL hooks
(`hook_setanimation.cpp`, `hook_vital_process_tick.cpp`, `hook_animal_death.cpp`):

- wolf→`male.dts` animation remap (so the player model animates),
- held-weapon auto-mount (image id `556`),
- the real server-side melee hit (`Animals::Animal::endAttack`) fired at the **contact frame**,
- the **stamina pool** that paces the swing (flurry → winded → recover),
- swing **protection** so the swing animation isn't overridden mid-play.

See [`docs/bandit_combat.md`](../../../docs/bandit_combat.md) for the mechanics, RVAs/offsets,
and console commands (`Lifx::banditStrike`, `Lifx::mountBanditWeapon`, …).

## The committed/rooted attack (`aiBanditAggressive.xml`)

The `AttackProcess` was changed from a `DynSelector` to a **`Sequence`** so a swing **plays
out** instead of being re-evaluated/aborted (and the bandit re-facing) every tick the player
dodges out of the attack sector. `ChaseEnemyNoAnimation` was removed (no lunge during the
swing), and an `AttackRecovery` hold (`Wait 2` — no animation, so it stays male.dts-safe; the
DLL swing-lock holds the swing's end pose) keeps it from chasing/re-facing between swings — so
the player can read the wind-up, leave the cone, and have the swing whiff (the DLL re-scans the
cone at the contact frame).

This tree is the **reconciled single source**: the #161 native-Animal re-platform (male.dts-safe
idle, 0..30 aggression) merged with the #154/#163 committed/rooted attack. The former standalone
`dist/ai/cmAiBandit.xml` duplicate was removed.

**Still open ([#163](https://github.com/Mj0ed/LiFxPluss/issues/163)):** fully freezing
translation + yaw during the swing window (move-engine), so the bandit can't track/slide
toward the player mid-swing.

## Deploy

Copy into the server, then register + hot-reload:

```
<server>/mods/HostileNPCs/mod.cs
<server>/mods/HostileNPCs/cmod.cs                 (client modpack)
<server>/.../art/datablocks/Bandit.cs
<server>/data/ai/aiBanditAggressive.xml           (BanditData.behavior resolves here)
```

> **Path note:** `mod.cs`/`cmod.cs` currently `loadRecursivelyInFolder` `Bandit.cs` from a
> hard-coded `yolauncher/modpack/mods/HostileNPCs/art/datablocks` path — adjust to your
> deployment layout. The behaviour XML must land at `data/ai/aiBanditAggressive.xml` (the
> engine resolves `BanditData.behavior` like the wolf's).

In the server console:
```
reloadBehaviorXml();
spawnObject("Wolf", "BanditData");   // or the GM serverCmdSpawnBandit / /animal BanditData
```
