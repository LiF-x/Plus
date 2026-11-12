# Outposts & claims

Commands for managing outpost defaults, live outpost edits, and the placement
distance rules between outposts, guild monuments, and personal claims.

Values use the corresponding command's engine convention. The legacy name
`radius` should not be treated as a universal radius-in-metres conversion;
check the resulting in-game boundary when calibrating a size change.

## Defaults for newly-built outposts

### `Lifx::getOutpostDefaultRadius()`

Return the radius that gets applied to a freshly-created outpost. Engine
baseline: **20**.

```tcl
%r = Lifx::getOutpostDefaultRadius();
```

### `Lifx::setOutpostDefaultRadius(int radius)`

Override the default radius. Affects only outposts created **after** this
call; existing outposts are not touched. Use `setOutpostRadius` to change an
existing one.

```tcl
Lifx::setOutpostDefaultRadius(40);
```

## Editing existing outposts

### `Lifx::setOutpostRadius(int guildLandID, int newRadius)`

Request a size change for an outpost that already exists. Use its
`guild_lands.ID`, not a claim ID or building ID. The command does not verify
that the supplied land belongs to an outpost, so resolve the intended land
first. The engine handles persistence and boundary rebuilding; the command
returns no success value, and its console echo alone does not confirm that
the resize succeeded. Check the saved size and visible boundary afterwards.

```tcl
Lifx::setOutpostRadius(101, 35);
```

### `Lifx::setOutpostProductionType(int unmovableObjectID, int typeID)`

Retarget an outpost's production to produce a different object type.

```tcl
Lifx::setOutpostProductionType(42, 707);
```

### `Lifx::dumpOutposts()`

List every live outpost on the server with their key fields: UnmovableObjectID,
outpostType, perTickCount, ProductionObjectTypeID, currentQuality.

```tcl
Lifx::dumpOutposts();
```

## Proximity rules

The engine refuses new outpost / monument placements that are too close to
existing ones. LiFx exposes the constants that gate those checks so server
operators can loosen or tighten the rules.

Each pair below is `get` + `set`; engine baselines are noted on the `get`.

### Guild monument ↔ guild monument

```tcl
%d = Lifx::getMonumentMinDistance();        // engine baseline: 150
Lifx::setMonumentMinDistance(100);
```

### Outpost ↔ outpost / guild-land

```tcl
%d = Lifx::getOutpostOutpostMinDistance();  // engine baseline: 300
Lifx::setOutpostOutpostMinDistance(250);
```

### Outpost ↔ personal claim

```tcl
%d = Lifx::getOutpostMinDistanceToPersonalClaim();  // engine baseline: 20
Lifx::setOutpostMinDistanceToPersonalClaim(15);
```

### Guild monument ↔ personal claim

```tcl
%d = Lifx::getMonumentMinDistanceToPersonalClaim();  // engine baseline: 20
Lifx::setMonumentMinDistanceToPersonalClaim(15);
```

## Notes

- Setting a value below the engine's hard internal minimums can produce
  surprising results (overlapping claims, broken monument geometry). Test
  small steps in a private world before deploying.
- The outpost-radius default is read at outpost-creation time; changing it
  later does not retroactively resize existing outposts.
- Default-size and proximity overrides last for the current process. Reapply
  persistent overrides through your startup scripts after command registration.
