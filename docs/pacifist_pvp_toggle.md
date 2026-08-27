---
title: Per-player pacifist (PvP-off) toggle
status: re
domain: lifx-framework
tags: [combat, pvp, damage, onepunchman, lifx-api, believer-weapon]
related: [character_hp.md, conventions.md]
sources: [source/server/hooks/character/hook_onepunchman.cpp, source/server/hooks/character/hook_onepunchman.h, source/server/api/lifx_character.cpp, source/server/cm_server.cpp, source/server/cm_offsets.h, source/server/hooks/furnace/engine_internals.h]
updated: 2026-06-26
---

# Per-player pacifist (PvP-off) toggle

Flag a player so that **any weapon they wield deals zero damage to other
players** — the generalised "believer weapon" behaviour, but per-player and for
*any* weapon (not a fixed set of item types). PvE (animals/NPCs) and damage to
the world are unaffected: only attacker→player hits are nullified.

## Commands

Run at the server console (or wire to slashcommands — see below):

```
Lifx::setPacifist(<charID>, 1);   # this player can no longer hurt other players
Lifx::setPacifist(<charID>, 0);   # restore normal PvP
Lifx::isPacifist(<charID>);       # -> "1" / "0"
Lifx::dumpPacifist();             # list every flagged charID
```

`charID` is the persistent character ID (the `character` table PK). For a
logged-in player you can read it in TorqueScript from
`GameConnection::getControlObject::getCharId`, the same accessor the HP commands
use (see [`character_hp.md`](character_hp.md)).

State is **in-memory** and resets on server restart. Persisting it (a small
table or config list re-applied on boot) is a possible follow-up.

### Slashcommand wiring (optional)

```cs
// in your server's TorqueScript startup
function serverCmdPacifist(%client, %charID, %on) {
    if (!%client.isAdmin()) return;          // gate however you like
    Lifx::setPacifist(%charID, %on);
}
```

Then an admin types `/pacifist 12345 1` in chat.

## How it works

The toggle is enforced in the **`ONEPUNCHMAN`** damage calculator
(`Hooks::OnePunchMan`, RVA `0x0A4BF0`, `hook_onepunchman.cpp`) — the engine's
central combat-damage function, called inside `Player::_applyHit`. It is the
only point in the damage path that carries **both** the attacker and the
defender context plus the output damage struct:

```
attackerCtx  (p1)  -> attacker charStats
outDamage    (p2)  -> { +0x00 hardHpDamage, +0x08 softHpDamage, +0x18.. wound aux }  (×1e6)
defenderCtx  (p5)  -> defender charStats
```

After the engine fills `outDamage`, the hook:

1. Resolves the attacker and defender to **live-player charIDs**.
2. If the **attacker is flagged pacifist** *and* the **defender is a player**,
   it zeroes the hard/soft HP-damage terms and the wound/injury aux block, so
   the hit lands for 0 — no HP loss, no wound, no bleed.

### Fail-safe identity resolution

The attacker/defender charID is read at `+0x109C` on the ctx pointer and then
**validated by round-tripping through the `charID → charStats` registry**
(`Hooks::VitalParams::LookupCharStats(cid) == ctx`, the same registry
`Process_tick` populates for every connected player). A ctx that does not
round-trip — an NPC, an animal, or any non-`charStats` struct — resolves to `0`
and the hit is left **completely untouched**. So the worst case is that the
feature is a silent no-op; it can never corrupt or break normal combat.

## Verifying it live

`ONEPUNCHMAN` was dormant (written but neither built nor attached) before this
change, so its ctx layout had no runtime confirmation. Enable LiFx debug
(`Lifx::setDebug(1)`) and land a real hit: the hook logs the first 30 calls

```
[lifx-pacifist] #<n>  atkCid=<A> defCid=<B>  (atk pacifist=<0|1>)
```

- Non-zero `atkCid`/`defCid` on a **player-vs-player** hit confirms the identity
  resolution works and the zeroing will fire.
- If both stay `0` on a real PvP hit, the ctx pointers are not the
  registry-keyed `charStats` and the resolution offset needs adjusting (the only
  thing that would need a fix — the rest of the wiring is correct).

When the flag fires you'll see:

```
[lifx-pacifist] PvP hit nullified: attacker charID=<A> -> player charID=<B> (hardHp .. softHp .. dropped to 0)
```

## Known limitation: ranged / bows

`ONEPUNCHMAN` is confirmed to be the **melee** damage path. Whether arrows /
ranged hits route through the same function is **not yet verified** — projectile
impacts often take a separate path. So melee is guaranteed; **bow support is a
follow-up**: if ranged uses a distinct impact function, the same pacifist check
(`Hooks::OnePunchMan::IsPacifist(atkCid)` + registry round-trip on the
defender) is added to a second hook on that function.

## Status & provenance

- **Verified (read in code / builds):** the `ONEPUNCHMAN` hook is wired into the
  build (`build_linux.sh`, `win/LiFx.vcxproj`) and attached in
  `Lifx::Server::AttachHooks` (`cm_server.cpp`); the `Lifx::setPacifist /
  isPacifist / dumpPacifist` console commands register
  (`lifx_character.cpp`); the DLL cross-compiles clean. The `charID → charStats`
  registry round-trip (`Hooks::VitalParams::LookupCharStats`) and the
  `+0x109C` charID / `0xAA8` charStats-to-Player deltas come from
  `engine_internals.h`, already used by the HP path.
- **Inferred / not yet runtime-verified:** that `ONEPUNCHMAN`'s `attackerCtx` /
  `defenderCtx` are the registry-keyed `charStats` with a valid `+0x109C` charID
  (plausible, layout-consistent, but never seen fire on a live two-player hit);
  the exact `outDamage` aux-field layout beyond the hard/soft HP terms; and
  whether **ranged/bow** damage routes through `ONEPUNCHMAN` at all (see the
  limitation above). The fail-safe round-trip means an unverified assumption
  degrades to a silent no-op, never to broken combat — confirm with
  `Lifx::setDebug(1)` telemetry on a real PvP hit.
- **Naming caveat:** the trampoline/handler are `_OnePunchMan` /
  `Hooks::OnePunchMan::Call`, which predate and don't follow the mandatory
  `_<Subsystem>_<Function>` / `Hooks::<Subsystem>::<Function>` form in
  [`conventions.md`](conventions.md). Bring it to `_Character_OnePunchMan` /
  `Hooks::Character::OnePunchMan` when the runtime behaviour is confirmed.
