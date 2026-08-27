# Timers — Resurrected duration

The Resurrected debuff (effect id 47) applied automatically after death has a
hardcoded duration of **10 minutes** (600 000 ms). LiFx exposes three policy
layers for changing that. Whenever the engine applies Resurrected to any
player, LiFx asks the layers in order — the first one that returns a non-zero
value wins.

**Priority** (highest first):

1. TorqueScript callback (`onResurrection`)
2. Per-player override (`setResurrectionFor`)
3. Global override (`resurrection`)
4. Engine default (10 min)

## Configuration sources

- `config/lifxpluss.xml` — `<resurrectionDurationMs>` seeds the global override
  at startup.
- TorqueScript — every command below is runtime-callable.

```xml
<!-- config/lifxpluss.xml -->
<resurrectionDurationMs>300000</resurrectionDurationMs>
```

## `LifxTimers::resurrection([int ms])`

Get or set the **global** Resurrected duration in milliseconds. `0` removes
the override and restores the engine default.

```tcl
LifxTimers::resurrection();            // echoes current value
LifxTimers::resurrection(300000);      // 5 minutes for everyone
LifxTimers::resurrection(0);           // back to engine default (10 min)
```

## `LifxTimers::setResurrectionFor(int charID, int ms)`

Set a per-player override. Takes precedence over the global value but is
overridden by an active `onResurrection` callback (see below). `ms = 0`
clears the entry.

```tcl
// 2 minutes only for this one player
LifxTimers::setResurrectionFor(1234, 120000);
```

## `LifxTimers::getResurrectionFor(int charID)`

Return the per-player override in ms. Returns `0` when no per-player override
is set (the global / engine default is in effect for that player).

```tcl
%ms = LifxTimers::getResurrectionFor(1234);
```

## `LifxTimers::clearResurrectionFor(int charID)`

Remove a per-player override. Equivalent to `setResurrectionFor(charID, 0)`.

```tcl
LifxTimers::clearResurrectionFor(1234);
```

## `LifxTimers::onResurrection(string scriptFnName)`

Register a TorqueScript function as the duration resolver. On every Resurrected
apply, LiFx invokes:

```
<scriptFnName>(<charID>, <engineDefaultMs>);
```

and uses the function's return value (parsed as a uint32) as the duration in ms.
Returning `0` or a non-numeric value falls through to the per-player /
global / engine layers.

Pass an empty string to clear the callback.

### Example: exponential backoff on recent deaths

```tcl
// Maintained in your existing death handler:
//   $lifxDeathCount[%charID]++;

function lifxResurrectionPolicy(%charID, %engineDefaultMs)
{
    %n = $lifxDeathCount[%charID];
    if (%n < 1) %n = 1;

    // 60s × 2^(n-1), capped at the engine default
    %ms = 60000 * (1 << (%n - 1));
    if (%ms > %engineDefaultMs) %ms = %engineDefaultMs;
    return %ms;
}

LifxTimers::onResurrection("lifxResurrectionPolicy");
```

### Clearing the callback

```tcl
LifxTimers::onResurrection("");
```
