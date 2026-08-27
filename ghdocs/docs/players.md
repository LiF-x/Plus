# Player HP & damage

All commands take a `charID` and operate on the connected character. HP values
are in **display units** (the integers you see in the UI), not in the engine's
fixed-point representation.

## Reading

### `Lifx::getPlayerHp(int charID)`

Return the player's current Hard HP. Returns `0` if the charID is not online or
not found.

```tcl
%hp = Lifx::getPlayerHp(1234);
```

### `Lifx::getPlayerSoftHp(int charID)`

Return the player's current Soft HP cap.

```tcl
%cap = Lifx::getPlayerSoftHp(1234);
```

## Writing

### `Lifx::setPlayerHp(int charID, int hp)`

Set Hard HP and persist the new value. The change is broadcast to the client.

```tcl
Lifx::setPlayerHp(1234, 75);
```

### `Lifx::setPlayerSoftHp(int charID, int softHp)`

Set the Soft HP cap and persist it.

```tcl
Lifx::setPlayerSoftHp(1234, 80);
```

### `Lifx::setPlayerLiveHp(int charID, int targetHp)`

Adjust a connected player's **live** HP to `targetHp` by computing the delta
and applying it as damage (or healing) through the engine's natural damage
path. Use this when you want HP changes to participate in normal gameplay
hooks (kill credit, on-damage scripts, etc.).

```tcl
Lifx::setPlayerLiveHp(1234, 50);
```

### `Lifx::setDamageLevel(int charID, int level)`

Directly set the `ShapeBase` damage level on a player. Bypasses the
applyDamage gating; use sparingly.

```tcl
Lifx::setDamageLevel(1234, 0);   // fully healthy
```

## Damage and healing

### `Lifx::applyDamage(int charID, int amount)`

Apply damage (positive `amount`) or healing (negative `amount`) to a connected
player's live HP. Goes through the engine's damage path.

```tcl
Lifx::applyDamage(1234, 15);     // deal 15 damage
Lifx::applyDamage(1234, -25);    // heal 25
```

### `Lifx::healToFull(int charID)`

Heal a connected player to full HP via the engine's natural healing pathway.

```tcl
Lifx::healToFull(1234);
```

## Knockout and kill

### `Lifx::knockout(int charID)`

Knock the player unconscious (the normal "downed" state used by the engine for
near-death and certain debuffs).

```tcl
Lifx::knockout(1234);
```

### `Lifx::kill(int charID)`

Kill the player. The engine processes the normal death pipeline (drop, run the
respawn/Resurrected flow, etc.).

```tcl
Lifx::kill(1234);
```
