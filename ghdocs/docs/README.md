# LiFx — TorqueScript API

LiFx extends a Life is Feudal: Your Own server with new TorqueScript-callable
commands. This is the public reference for those commands — what they do, how
to call them, and the values they return.

Everything documented here is invokable from server-side TorqueScript (and from
the in-game admin console where TorqueScript is accepted).

## Sections

| Page | Purpose |
|---|---|
| [Effects on a player](effects.md) | Modify an active effect on a `Player` object (expiry, extend, clear, force a HUD refresh). |
| [Timers — Resurrected duration](timers.md) | Override the duration of the post-respawn Resurrected debuff, globally or per player, with optional callback-based policy. |
| [Player HP & damage](players.md) | Read and write a player's HP, apply damage / heal, knock out, kill. |
| [Outposts & claims](outposts.md) | Default outpost radius, live radius changes, production retargeting, and the proximity rules between outposts / monuments / personal claims. |
| [NPC spawning](npcs.md) | Spawn NPCs, keep them respawned near players, and bind their appearance / equipment loadout. |
| [Dispatcher — sector handoff](dispatcher.md) | Move characters and forward frames between server peers for multi-shard / seamless sector handoff (experimental). |

## Conventions used in this reference

- A "charID" is the engine's uint32 character identifier — what
  `LiFxUtility::getPlayer(<charID>)` looks up.
- Durations are **milliseconds** unless otherwise stated.
- `%p` in examples is a server-side `Player` object (typically obtained via
  `%c = LiFxUtility::getPlayer(<charID>); %p = %c.getControlObject();`).
- "Engine default" means LiFx is not modifying the value — the game's own
  baseline is in effect.

## Where this lives

Source of truth lives in the LiFx project. This documentation set is mirrored
to [Rampart-Games-Limited/LiFxRampart](https://github.com/Rampart-Games-Limited/LiFxRampart).
