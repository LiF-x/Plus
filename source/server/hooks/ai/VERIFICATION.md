# Verifying the custom AI nodes

## GoToPoint (issue #123) — VERIFIED on `_c` (2026-06-15)

First **action** node — drives real creature movement. Put
`<node class="GoToPoint" value="-234 -1258 1078"/>` in the grouse peaceful
branch and observed a grouse walk to the coordinate:

```
[lifx-ai] GoToPointLoad: value='-234 -1258 1078' -> dest=(-234,-1258,1078)   # loadFromXml
[lifx-ai] GoToPoint -> moving to (-234,-1258,1078)                            # set move target
[lifx-ai] GoToPoint -> arrived (1)                                           # ~19s later, arrival detected
```

- Cloned from `GoToPosition` for its larger layout (dest at +0x40/+0x44/+0x48,
  moving-flag at +0x4c), with a coord-parsing `loadFromXml`.
- Own `process` resolves the creature from the AI-context blackboard
  (`*(node+0x28)`), which is keyed by class — **`"animal"`** for animals,
  **`"npcbase"`** for NPCs. `GoToPosition` is hard-wired to `"npcbase"` (hence
  the initial `npc is nullptr` on a grouse); `GoToPoint` tries `"animal"` then
  `"npcbase"`, so it works for either. Then `getEngine(creature)` (`0x2E3380`)
  → `setMoveTarget(engine, &dest)` (`0x14014FD50`), polls arrival
  (`0x14014FD30` == 1) and stops (`0x14014FE40`); returns 3 (moving) / 1
  (arrived). See ABI_NOTES.md.

This is the movement primitive the caravan `WalkWaypoints` node builds on.


## TimeOfDayBetween / IsNight (issue #121) — VERIFIED on `_c` (2026-06-15)

Inserted `<node class="TimeOfDayBetween" value="20 6"/>` (Failurator-wrapped)
into the grouse tree and observed, once a grouse ticked near a player:

```
[lifx-ai] TodLoad: value='20 6' -> start=20 end=6        # real loadFromXml parsed the value
[lifx-ai] TimeOfDayBetween hour=11 range=[20,6)          # real process read the live in-game hour
```

- `loadFromXml` parses `value="start end"` via the TiXml1 attribute getter. ✓
- The per-creature clone preserves the parsed range (clone copies the `+0x40`
  param the borrowed `Stopped` clone doesn't know about). ✓
- `process` reads the **in-game** hour (see ABI_NOTES.md — `hour=10`→`11` over
  ~9 real-minutes, not wall-clock) and evaluates range membership; at hour 11
  with the night range `[20,6)` it correctly returns failure (daytime). ✓

Per-tick debug logging was removed after verification (a condition node is
silent); the one-shot `TodLoad` line remains as authoring feedback.

# Verifying the LifxLogNode PoC (issue #119)

## Result: VERIFIED end-to-end (2026-06-15, server `_c`)

All three gates passed on a live boot:

```
[lifx-ai] behavior-XML loader hook attached at RVA 0x153B80 (registers LifxLogNode)
[lifx-ai] registered behavior node class 'LifxLogNode'
Loading AI behaviour from [data/ai/cmAiGrouse.xml]        <- no _createNode error
[lifx-ai] LifxLogNode tick #1 (self=00007FAD4B0A4C90)     <- engine ticks our node
[lifx-ai] LifxLogNode tick #2 ... #3 ... #4 ...
```

A grouse auto-spawned via the spawn-pattern system and ran the tree containing
`<node class="LifxLogNode"/>`; the engine ticks our custom node every frame.
Server stayed stable.

Two environment fixes were needed first (not code — local to the box):
- `mariadb-upgrade` — MariaDB had been upgraded (12.1→12.3) leaving a stale
  `mysql.proc` (error #1558), which broke `sql/patch.sql` and aborted world init.
- Added `config/lifxpluss.xml` to `_c` (it had never run the LiFx C++ DLL, so
  the config was absent and `Init()` errored before hooks could register).

And one code fix: the registration hook was moved from
`BehaviorsManager::reloadBehaviorXML` (`0x1506D0`, only the manual console
reload) to the per-tree loader (`0x153B80`), which the boot path actually calls.

---

What was done before the live run (repo / build host):

- **ABI reverse-engineered** and recorded in `ABI_NOTES.md`.
- **Code compiles**: `./build_linux.sh lifx` produces
  `win/build/Release/4ba5cb5e.dll` with no errors (only the pre-existing
  `-Wmicrosoft-cast` warnings shared by every hook).

What still needs a **live server run** (your Wine environment — the mod loads
via the `pdh.dll` proxy, which is not deployed on this machine):

## Test artifacts already placed on the `_c` server

- `lif_server_320850_c/data/ai/cmAiGrouse.xml` — one test node inserted at the
  top of `ThreatValueManager` (Failurator-wrapped so it ticks every frame
  without disrupting the grouse AI). Original backed up as
  `cmAiGrouse.xml.lifxbak`.
- `lif_server_320850_c/data/ai/lifxTest.xml` — standalone test tree
  (alternative to editing a vanilla tree).

## Steps

1. **Negative control (stock binary).** With the *unmodified* server (no
   LiFx DLL), start it and watch the AI load. The grouse tree (now containing
   `class="LifxLogNode"`) must make the loader **bail / log "class not found"**
   — confirming the name is genuinely not built-in. (Or run
   `registerBehavior("lifxTest","data/ai/lifxTest.xml"); reloadBehaviorXml();`
   and see it reject `LifxLogNode`.)

2. **Deploy** the built `4ba5cb5e.dll` (+ the `pdh.dll` proxy) into the server
   install, as you normally deploy LiFx.

3. **Hook + registration proof.** Start the server. Expect on boot:
   ```
   [lifx-ai] reloadBehaviorXML hook attached at RVA 0x1506D0 (registers LifxLogNode)
   [lifx-ai] registered behavior node class 'LifxLogNode'
   ```
   The second line prints the first time the engine (re)loads behavior trees.

4. **Load proof.** In the server console:
   ```
   reloadBehaviorXml();
   printBehaviorTree("cmAiGrouse");
   ```
   The tree now loads with `LifxLogNode` present (no "class not found" bail) —
   registration worked.

5. **Tick proof.** Let a grouse spawn / be near (it ticks its tree). Watch for:
   ```
   [lifx-ai] LifxLogNode tick #1 (self=...)
   ...
   [lifx-ai] LifxLogNode tick #5 (self=...)
   ```
   (throttled to the first 5 then every 500th). This proves a *new* node
   primitive is being evaluated by the engine each tick.

6. **Clean detach.** Stop the server; confirm a clean shutdown (the node's
   instances are freed via the template's own destructor — no crash).

## Revert the test edits

```
cd lif_server_320850_c/data/ai
mv cmAiGrouse.xml.lifxbak cmAiGrouse.xml
rm -f lifxTest.xml
```
