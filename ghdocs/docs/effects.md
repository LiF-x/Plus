# Effects on a player

These documentation updates were generated with help from ChatGPT (Codex),
in collaboration with Zbigi, from notes and analyses collected over four months
of work on LiFXXL, a community project based on a modified LiF:YO server.

API documentation update (2026-11-11).

These commands are **methods on a server-side `Player` object** — invoke them
through `%p` after resolving the Player from a charID.

```tcl
%c = LiFxUtility::getPlayer(<charID>);
%p = %c.getControlObject();
```

All four commands request an effect update for clients that have this player
ghosted. A successful send attempt in the server log does not confirm that a client
has received or displayed the update. Check the effect HUD and gameplay state when
validating a change; no exact network-delivery or stat-recalculation timing is
guaranteed here.

Use supported effect IDs and existing effect state. These commands edit or resend
that state; they are not an API for applying a new effect. Their validation does
not establish that an effect is currently active.

Restrict effect IDs to supported catalog entries before invoking the command.
The matching-ID check does not safely validate an arbitrary out-of-range ID.
Use valid nonnegative integer arguments. Time values must fit in the unsigned
32-bit range (`0..4294967295`); extending past that limit wraps the stored time
instead of reporting an error. Negative values are not a supported way to shorten
an effect.

## `%p.lifxSetEffectExpiry(int effectID, int expires_at_ms)`

Set the absolute expiry time of one effect on this player. `expires_at_ms` is
the global server clock value at which the effect ends.

```tcl
// Make effect 47 (Resurrected) end at server-clock time 800000
%p.lifxSetEffectExpiry(47, 800000);
```

The command checks that the requested effect entry has a matching ID. A mismatch
is rejected with a warning; inactivity alone is not the condition checked. For
Resurrected (47), the configured [duration policy](timers.md) can replace the
requested duration when the update is sent.

## `%p.lifxExtendEffect(int effectID, int extra_ms)`

Add `extra_ms` milliseconds to the stored expiry. Use this for an effect you know
is active; the command itself does not establish that condition. The Resurrected
duration policy can also apply to this update.

```tcl
// Request 2 more minutes; a configured Resurrected policy may override this
%p.lifxExtendEffect(47, 120000);
```

## `%p.lifxClearEffect(int effectID)`

Set the effect's expiry and magnitude to zero, then request an update to clients.
This is intended to expire the effect and remove its intensity. The engine must
re-evaluate gameplay state and the client must process the update; do not rely on
the icon and every derived stat changing in the same tick as this command.

```tcl
// Expire Resurrected and request the client update
%p.lifxClearEffect(47);
```

## `%p.lifxBroadcastEffect(int effectID)`

Request a resend of the current effect state to clients ghosting this player.
The command does not make an expiry or magnitude edit itself. The normal duration
policy still applies: a Resurrected entry whose expiry is later than its recorded
application time can have its duration replaced by the configured override. That
comparison does not check whether the expiry is still in the future. Primarily
useful for checking delivery or recovering
from a desynced HUD.

```tcl
%p.lifxBroadcastEffect(47);
```

If an update cannot be sent after an editing command, the server-side edit is not
automatically undone. Inspect warnings and verify the resulting player state.

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
