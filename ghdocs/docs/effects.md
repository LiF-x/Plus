# Effects on a player

These commands are **methods on a server-side `Player` object** — invoke them
through `%p` after resolving the Player from a charID.

```tcl
%c = LiFxUtility::getPlayer(<charID>);
%p = %c.getControlObject();
```

All four commands push the resulting effect change to every client that has the
player ghosted, so the in-game effect HUD updates immediately.

## `%p.lifxSetEffectExpiry(int effectID, int expires_at_ms)`

Set the absolute expiry time of one effect on this player. `expires_at_ms` is
the global server clock value at which the effect ends.

```tcl
// Make effect 47 (Resurrected) end at server-clock time 800000
%p.lifxSetEffectExpiry(47, 800000);
```

If the effect is not currently active on the player, the call is rejected and
a warning is logged.

## `%p.lifxExtendEffect(int effectID, int extra_ms)`

Extend an active effect by `extra_ms` milliseconds — equivalent to bumping its
`expires_at` forward by that many ms.

```tcl
// Give the player 2 more minutes of Resurrected
%p.lifxExtendEffect(47, 120000);
```

## `%p.lifxClearEffect(int effectID)`

Immediately end an effect. The icon disappears from the player's HUD and the
effect's gameplay penalties stop applying on the same tick.

```tcl
// Remove Resurrected sickness right now
%p.lifxClearEffect(47);
```

## `%p.lifxBroadcastEffect(int effectID)`

Re-send the current state of an effect to all clients without modifying the
underlying data. Primarily useful for testing the broadcast path or recovering
from a desynced HUD; rarely needed in production scripts.

```tcl
%p.lifxBroadcastEffect(47);
```

## Effect IDs

Effect IDs match the entries in `data/cm_effects.xml` (the engine's effect
catalog). Some commonly-used IDs:

| ID | Name |
|---:|---|
| 47 | Resurrected (post-death sickness) |
| 45 | Invulnerable |
| 46 | Criminal |
| 66 | Barefooted |

Consult `data/cm_effects.xml` for the full list.
