# NPC spawning

Commands for spawning NPCs, keeping them respawned near players, and binding
their appearance/equipment. World coordinates are in engine units (meters); a
`charID` is the engine's uint32 character identifier.

> **Experimental.** NPC equipment-render and character-bind behaviour is still
> being hardened. Spawn commands are stable; the bind/loadout/ghost methods
> depend on the LiFx client DLL being installed and may change between releases.

## Spawning

### `Lifx::spawnNpcDecorative(float x, float y, float z [, string dataBlock])`

Spawn an `NPCS::NPCDecorative` at the given world coordinates. `dataBlock`
defaults to `NPC_slave_A`. Echoes the new object id to the console.

```tcl
Lifx::spawnNpcDecorative(1520.0, 980.0, 130.0);
Lifx::spawnNpcDecorative(1520.0, 980.0, 130.0, "NPC_slave_A");
```

### `Lifx::spawnNpcAtChar(int charID [, string dataBlock])`

Spawn an `NPCS::NPCDecorative` right beside a connected character — no
coordinates needed. Echoes the new object id.

```tcl
Lifx::spawnNpcAtChar(1234);
```

### `Lifx::makePlayerNpc(int charID)`

Define a player-model `NPCData` (using `male.dts`) if it doesn't exist yet and
spawn it beside the character. A player body carries the armor meshes that
equipment rendering needs, so use this when you want an NPC that can display
worn gear.

```tcl
Lifx::makePlayerNpc(1234);
```

## Managed (respawning) spawns

### `Lifx::manageSpawn(float x, float y, float z, string dataBlock [, int loadout])`

Register a respawning NPC node, outpost-style: the maintenance tick keeps it
spawned while a player is present, the engine despawns it when the player
disconnects, and the tick respawns it when a player returns. The `dataBlock`
**must exist at server startup**.

The maintenance tick is started lazily on the first `manageSpawn` call (it
cannot be started at registration time, before the Sim event queue exists).

```tcl
Lifx::manageSpawn(1520.0, 980.0, 130.0, "NPC_slave_A");
Lifx::manageSpawn(1520.0, 980.0, 130.0, "NPC_slave_A", 1);
```

### `Lifx::ensureSpawns()`

Run one managed-spawn maintenance pass manually. Normally driven by the
`lifxEnsureSpawns` schedule tick; call it directly to force an immediate pass.

```tcl
Lifx::ensureSpawns();
```

## NPC appearance & equipment (SimObject methods)

These are registered as `SimObject` methods, so call them on a resolved NPC
object (`%npc`). The callback receives the NPC pointer directly.

### `%npc.lifxBindNpc(int charID)`

Bind this NPC's `charStats` to a registered character id so the engine resolves
its `CmPlayerEquipment` (equipment render + worn-loot).

```tcl
%npc.lifxBindNpc(1234);
```

### `%npc.lifxLoadout(int id)`

Set which equipment loadout this NPC renders: `0` = plate, `1` = leather.
Pushed to clients on the next ghost update.

```tcl
%npc.lifxLoadout(0);
```

### `%npc.lifxGhost()`

Force a script-spawned NPC to ghost to clients via
`NetObject::setScopeAlways`, so clients out of normal scope still see it.

```tcl
%npc.lifxGhost();
```

### `%npc.lifxAiTick([int on=1])`

Enable (or disable with `0`) per-pack behaviour-tree ticking for this NPC.
`NPCDecorative` does not tick its tree natively, so call this after
`setBehavior()` if you want the tree to run.

```tcl
%npc.lifxAiTick();      // enable
%npc.lifxAiTick(0);     // disable
```

### `Lifx::nakedNpc(int charID)`

Tell a character's client to cull armor meshes on player-model NPCs (naked
body). Requires the LiFx client DLL to be installed on that client.

```tcl
Lifx::nakedNpc(1234);
```

## Notes

- The equipment-render path (`lifxBindNpc` / `lifxLoadout` / `nakedNpc`)
  depends on the LiFx client DLL; without it, clients render the NPC's default
  appearance.
- Managed spawns despawn on player disconnect by design — they are not
  permanent world entities. Use a plain `spawnNpcDecorative` for a one-off that
  should persist independently of player presence.
