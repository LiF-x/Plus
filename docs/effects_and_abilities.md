---
title: Effects & abilities
status: re
domain: reverse-engineering
tags: [effects, abilities, rtti, xml-parser]
related: [character_hp.md, ai_and_spawning.md]
updated: 2026-06-26
---

# Effects and Abilities

How the effect / ability subsystem in `ddctd_cm_yo_server.exe` is shaped, what we've located, and which seams LiFx can hook to extend it without rebuilding the engine.

## TL;DR

- **Effects** (the buff/debuff entities in `data/cm_effects.xml`) are parsed at boot by a single dispatcher at RVA **`0x4DD100`** (`FUN_1404dd100`). This function references 25 of the 26 distinct parameter-type/applytype tokens that appear in `cm_effects.xml` (`SPEED`, `HARD_HP_MAX`, `CONSUME`, `INCREASE_COEFF`, etc.) — i.e. it *is* the string-to-enum effect parser. Hooking it on entry/exit gives a complete inventory of every effect the server knows about, and on exit lets LiFx mutate that inventory.
- **Abilities** (the verbs in `data/admin_lands_abilities.xml`, `data/cm_special_attacks.xml`, and the in-game action wheel) are **vtable-dispatched, one C++ class per ability**. RTTI recovers **311 `*_Ability` classes** (`Build_Ability`, `Cook_Ability`, `ApplyPoison_Ability`, `BlessGodsLove_Ability`, …) all sharing a base `Ability` vtable whose **slot 6 is `_purecall`** — that is the pure-virtual main action method every concrete ability must implement.
- Adding a brand-new C++ ability class is infeasible without engine source. But two seams *are* viable:
  1. **Hook a base-class vtable slot** (e.g. slot 0 destructor or one of the shared inherited slots) to intercept every ability call site at once. Filter at runtime by `this`-pointer class identity.
  2. **Hook the ability constructor/registrar** to remap existing ability instances to scripted behavior — repurposing rarely-used `_Ability` classes as handles for new gameplay.
- For effects, adding new entries is straightforward once `0x4DD100` is hooked: we can post-process the parsed effect table to splice in new IDs *or* synthesize them from a LiFx-side XML file. The hard constraint is that the **parameter-type enum** (`SPEED`, `HARD_HP_MAX`, …) is baked into the exe — novel parameter types require also hooking the per-tick effect evaluator (not yet located).

## How we found this

Two-anchor reverse-engineering pass (`scripts/ghidra/LifxEffectsScan.java`, run via the standard headless workflow in [`reverse_engineering.md`](reverse_engineering.md)):

1. **String anchors.** Cross-referenced every parameter-type token enumerated from `data/cm_effects.xml` against the binary's intact `.rdata` strings, recording which functions read each.
2. **RTTI anchors.** Walked every recovered class namespace, filtering for `*Effect*`, `*Ability*`, `*SpecialAttack*`, `*Trigger*`. Dumped the first vftable of each match and compared slot targets across siblings to identify which slots are inherited vs. overridden.

Outputs under `/tmp/lifx_ghidra/effects_*.tsv`.

### The effect parser — fan-in evidence

```
distinct_tokens  total_hits  func_rva   func             tokens
25               26          0x4DD100   FUN_1404dd100    ATTACK_SPEED,BAREFOOTED_SPEED,BIND_TO_EFFECT_LIFETIME,
                                                          CAST_DURATION,CONSUME,DECREASE,DECREASE_COEFF,DEFENCE,
                                                          DRINK_IMMUNE,HARD_HP,HARD_HP_MAX,HARD_HP_REGEN_SPEED,
                                                          HARD_STAMINA,HARD_STAMINA_REGEN_SPEED,INCREASE,
                                                          INCREASE_COEFF,MULTIPLY,POISON_IMMUNE,RANGED_ATTACK_SPEED,
                                                          REGENERATE,SKILL,SKILL_GROW_BONUS_MULT,SOFT_HP,SPEED,TITLE
1                1           0x44FE0    FUN_140044fe0    TITLE
```

The token `TITLE` shows up in a second function (`FUN_140044fe0`) once — likely the slave/title display path (`Enslaved` effect, ID 91, uses `parameter type="TITLE"`). All other tokens are unique to the effect parser. The four stat-name tokens absent from the table (`AGI`, `CON`, `INT`, `LUCK`, `DAMAGE`, `FEAR`, `SICK`, `RESTING`) almost certainly share the engine's stat-name table at a different RVA and are not yet enumerated by the scan — they don't change the identification of `0x4DD100` as the effect parser.

### The ability vtable — inheritance evidence

Base `Ability` class vftable at RVA **`0x7F59D8`**:

| Slot | Target (base) | Notes |
|------|---------------|-------|
| 0 | `FUN_140086850` | shared dtor stub |
| 1 | `FUN_140318810` | base inherited |
| 2 | `FUN_1403294a0` | base inherited |
| 3 | `FUN_1403299c0` | base inherited |
| 4 | `FUN_140329350` | base inherited |
| 5 | `FUN_1403352c0` | base inherited |
| **6** | **`_purecall`** | **pure-virtual main action — every concrete `*_Ability` overrides this** |
| 7 | `FUN_14031c4d0` | base inherited |
| 8 | `FUN_140325390` | base inherited |
| 9 | `FUN_140325400` | base inherited |
| 12 | `FUN_140325270` | base inherited |
| 13 | `FUN_14009de40` | base inherited |
| 15 | `FUN_14031a7f0` | base inherited |

Three sampled subclasses (`Build_Ability`, `Cook_Ability`, `ApplyPoison_Ability`) all keep slots 3, 4, 5, 8, 9, 12, 13 identical to the base, and all three **override slot 6** with class-specific code (`0x32AB70` / `0x32B080` / `0x32A4B0`). Slots 1, 2, 7, 15 also vary across subclasses (likely `getName` / `getCooldown` / `canCast` / `serialize` accessors).

The full vtable dump for all 311 classes is in `/tmp/lifx_ghidra/effects_class_vtables.tsv`.

## Hookable seams

Per [`conventions.md`](conventions.md), each seam below would land in `Lifx::Server::AttachHooks()` in `cm_server.cpp` (the empty gameplay-side stub), not the engine-side `hooks_engine.cpp`.

### Seam A — effect parser (`0x4DD100`)

**Subsystem name:** `Effect`. **Offset:** `EFFECT_PARSE`.

- **What it gives us:** complete observability over every effect the server registers at boot, plus a post-call site where LiFx can splice in new effect IDs that reuse existing parameter types.
- **Why first:** fires deterministically at startup → easy to verify the seam works by logging via `Con::Echo`.
- **Limitation:** boot-time only; doesn't help with effect *application* at runtime (that's a separate function, not yet located).

### Seam B — base `Ability` vtable patch (`0x7F59D8`)

- **What it gives us:** a single intercept point for every ability invocation (any of the 311 subclasses). Patch one slot of the base vtable and you observe every ability fire.
- **Limitation:** vtable patching is more invasive than a Detours hook on a free function — need to be careful about object-layout assumptions and per-class overrides. Wait until Seam A is proven before attempting.

### Seam C — TorqueScript callback bridge

Independent of the C++ hook above: once any gameplay hook is wired, expose a TorqueScript callback name (e.g. `LifxOnEffectParsed(effectId, name)`) via `Con::Evaluate`. From that point, new effect/ability behaviors can be authored in `.cs` files and reloaded live — no LiFx rebuild required. This is the high-leverage payoff.

## What's not yet known

| Question | Status |
|---|---|
| Where does `data/admin_lands_abilities.xml` get parsed? | Not yet located. The 311 `_Ability` classes themselves are *code*, not data — the XML probably configures per-ability *properties* (range, cooldown, animation). |
| Where is an effect applied to a character at runtime? | Not yet located. Search next: cross-refs of the parsed effect table (whose address is the return value of `FUN_1404dd100`). |
| Where is the stat-name enum (`AGI`/`CON`/`INT`/`LUCK`/`DAMAGE`/`FEAR`)? | Not yet located — they're absent from the fan-in scan, so they live in a different table (likely a generic stat-name registry). |
| What does each varying ability vtable slot mean? | Sampled 3 of 311. A full diff across all subclasses would let us label slots `1`/`2`/`7`/`15` definitively. |

These become the follow-up issues once the first hook (Seam A) is in.

## Resurrection sickness — apply-site investigation (incomplete)

Tracking issue #30. The "Resurrected" effect (ID 47, description id 2636 "Resurrected recently") has the typical data-driven magnitude row in `cm_effects.xml` — those are editable today — but its **duration** is supplied by the engine code that applies the effect after a player respawn, and that code path is not yet located despite four targeted static-analysis passes.

### Static anchors tried (all rejected)

| Anchor | Outcome |
|---|---|
| String `"Resurrected"` fan-in | One hit: `FUN_1404dcc80` @ `0x4DCC80`, but it's the **effect-ID → name string lookup** (94-case switch returning `"Root"`, `"Stun"`, …, `"Resurrected"`). Sole caller is the debug serializer — not apply path. |
| Immediate-`47` fan-in | 255 functions reference 47 as a constant; only the name lookup also references "Resurrected". 47 alone is far too common (lots of unrelated 47-byte buffers, 47-second timers, etc.) to be a useful filter. |
| Message id `2636` immediate | 13 functions reference 2636; cluster spread across `0xC2xxx`–`0xDBxxx` and `0x3E4xxx`–`0x3E6xxx` with no dominant hit. |
| Container accessors `FUN_1403cb9a0` / `FUN_1403cb990` (used by debug serializer to iterate effects on a character at offset `+0x10E0`) | 3 + 4 callers respectively. The promising-looking add/update pair `FUN_1400ec580` / `FUN_1400f0ee0` (called from the `0x377xxx` ability region with a tier-like `int` param) turned out to be **`CreaturesSkills::UnitsFormation::create`** — assert string `"engine\source\app\unit\unitsformation.cpp"` confirms it's squad-formation code, not effect application. The container at `+0x1B88` they manipulate is the formation member list, not the effect list. |

### Useful confirmed artifacts (keep for future)

- **`FUN_1404dcc80` @ `0x4DCC80`** = canonical effect-ID → name lookup. Gives us the engine's internal name for every effect ID 0–93 (`Root`, `Stun`, `Resurrected`, …) — useful for any future debug-print or tooling that needs the engine-canonical name.
- **`FUN_140247260` @ `0x247260`** = sole caller of the effect parser at `0x4DD100`; lives inside `Complex::ServerObjectStorage` construction. Confirms boot-time timing of Seam A.
- **`FUN_14009f730` @ `0x9F730`** = character-state debug serializer. Iterates the effect list on a character at byte offset `+0x10E0` and emits a `// CharacterParameters:` block. Best static handle we have on "where the effect list lives in the Character struct", though the iteration code uses generic offset-getter helpers (also used by unrelated formation code), so the offset doesn't transfer cleanly to a fan-in scan.

### Why static analysis hit its ceiling

The engine's effect/ability code is dispatched through a manager-class hierarchy whose member functions are inlined by LTO or templated to the point that they share entry points with unrelated systems (units, formations, datablocks). The container accessors that *look* like vector operations on the effect list also operate on every other byte-offset-`+0x150`-shaped struct member in the binary. Without per-class symbol info (which the PDB-stripped build doesn't give us beyond RTTI), the cleanest distinguishing feature — "writes the resurrection effect ID into a Character's effect list" — has no single anchor a string-or-immediate scan can isolate.

### Next move (requires runtime instrumentation, not a single PR)

The path forward is **observation-driven**, not pattern-driven:

1. **Capture the parsed effect-table address at boot.** Extend the existing Seam A hook in `source/server/hooks/effect/hook_effect_parse.cpp` so that after `_Effect_Parse()` returns, it walks the engine's effect descriptors and finds the address of the entry for ID 47. Log it.
2. **Snapshot a character's effect list around the moment of resurrection.** Add a `Lifx::dumpEffects(charID)` console command that, given a Character pointer (resolvable via existing LiFx character APIs), prints the current contents of the `+0x10E0` effect list with each entry's expiry timestamp. Kill a test character, resurrect, run the command — the new entry IS the resurrection effect, and its expiry timestamp IS the duration the engine chose.
3. **Diff the entry's bytes against `cm_effects.xml` ID 47's `<parameter>` rows** to identify which field encodes duration (likely a relative ms offset or an absolute tick deadline).
4. **Patch the offset.** Once the duration field is identified, a single byte-level write on the in-memory entry (or a hook on whatever advances the expiry tick) gives us live editable duration.

The total work is ~150–300 lines of new C++ across two hooks + one console command, but it has to be staged across at least two PRs (instrumentation first, observation second, mutation third) because each step depends on results from the previous one. Filing as follow-up issues to #30.

**For now, the only editable resurrection-sickness knob is the magnitude rows in `cm_effects.xml` (e.g. the `value="0.1"` on the HP_MAX decrease) — duration remains engine-controlled until the runtime-instrumentation path is built out.**

### Runtime instrumentation: what's built and what's been observed (WIP)

`source/server/api/lifx_effects.{h,cpp}` exposes a set of TorqueScript probes (registered from `hooks_engine.cpp::ConsoleInit`):

| Command | Purpose |
|---|---|
| `Lifx::dumpEffects(charID)` | First-iteration dump using the offsets guessed from the debug serializer — **proved wrong**: count field at `+0x1228` returned garbage (4-billion), sentinel at `+0x1230` dereferences to NULL even when effects are active. |
| `Lifx::dumpCharScan(charID [, startOff, endOff])` | Wide scan of the CmCharacterInfo struct, classifies each 8-byte qword as `PTR-arena` / `small-int` / `ptr-self`. |
| `Lifx::findEffect(charID, effectID [, startOff, endOff])` | Three-tier search for a literal `uint32 == effectID`: inline in struct, one pointer-follow with 512-byte probe, two pointer-follows. |
| `Lifx::dumpAt(charID, hex offset [, byteCount])` | Hex dump bytes at character + offset. |
| `Lifx::dumpPtr(hex pointer [, byteCount])` | Hex dump bytes at an absolute address. |
| `Lifx::snapshotChar(charID [, startOff, endOff])` | Captures up to 64 KB of the struct into a static buffer. |
| `Lifx::diffChar()` | Reports every 4-byte word that changed since the last snapshot. |

What we've observed in vivo:

- **Effect ID 66 (Barefooted)** appears at exactly one inline offset: `+0x2AF4`. With Barefooted active or inactive, the bytes around it (`00 00 00 00  42 00 00 00  00 00 00 00 00 00 00 00`) are unchanged. Strongly suggests this region is the **static effect-definitions table**, not per-character active state.
- The static table pattern (`00 00 00 00  [ID]  00 00 00 00 00 00 00 00 00 00 00 00`) holds for IDs ≥50 starting at `+0x29F0`, in 16-byte rows. Rows for IDs <50 live in a different shape (`02 00 02 00 ...` prefix) immediately above `+0x29F0`. The lower-ID rows look like a different table interleaved into the same memory region.
- **Effect ID 47 (Resurrected)** appears at two inline offsets — `+0x1A50` and `+0x243C`. With resurrection sickness active and ticking down for 60+ seconds, both regions are byte-identical between snapshots. Neither holds the live timer.
- **Full 64 KB snapshot/diff with Resurrected actively ticking down for 80 seconds: zero 4-byte words changed.** Strong evidence the resurrection-sickness countdown isn't stored as a mutating counter anywhere in the first 64 KB of CmCharacterInfo.
- **Snapshot → cast a 5-second ability → diff produces exactly one CHANGED word**: `+0x9A40  7FC00000 → 7F800000` (qNaN ↔ +∞). That's a single ability-related slot, not an effect-list field. But it proves the snapshot/diff machinery works end-to-end and that genuine state changes do produce CHANGED lines.

### What this tells us about where the active state lives

Combining the above: when resurrection sickness is on a character, *nothing* in the first 64 KB of CmCharacterInfo changes over a minute-plus window. Either:

1. **The duration is stored as an absolute `expires_at` timestamp set once when the effect is applied** (never mutated until the effect is removed), so the snapshot/diff window catches no motion. To find it: snapshot with a clean character, kill them, let resurrection sickness apply, then diff — the *newly non-zero* bytes ARE the entry.
2. **Active effect state lives on the Player object** (the in-world ShapeBase avatar), not CmCharacterInfo. `engine_internals.h` notes `Player + 0xAA8 = charStats`, suggesting CmCharacterInfo and Player are sibling objects sharing some fields. The active-effects list would more naturally live on Player.
3. **Active state lives past `+0x10000`**. CmCharacterInfo may be bigger than 64 KB.

The next experiment chain is: (a) extend the snapshot range to 128 KB or larger; (b) add `Lifx::snapshotPlayer(...)` that snapshots `Character_GetByID(charID) - 0xAA8`; (c) snapshot + die + diff, looking for the new effect entry's birth bytes.

### Followup runtime probes added (Session 2)

`Lifx::snapshotPlayer(charID)`, `Lifx::diffPlayer()`, `Lifx::findPlayer(charID)` — all in `source/server/api/lifx_effects.cpp`. Findings from in-vivo testing:

- The `Player + 0xAA8 = charStats` relationship from `engine_internals.h` **does not hold** for our engine version when applied as `Player = CmCharacterInfo - 0xAA8`. The stamp at `CmCharacterInfo - 0xAA8 + 0x1B44` decodes to ASCII characters (`0x496F6547` = "GeoI"), not the requested charID. Either the offset is different or CmCharacterInfo isn't a sibling of charStats inside Player.
- The fallback `findPlayer` scan — walk every user-space pointer in CmCharacterInfo's first 64 KB and read its target at +0x1B44 looking for the charID stamp — **also returned no match**. So either:
  1. CmCharacterInfo has no direct pointer to its Player at any 8-aligned offset in the first 64 KB.
  2. The charID stamp in Player lives at a byte offset other than +0x1B44.

The runtime instrumentation path has therefore exhausted the easy moves. To find the Player object reliably, the remaining options are:

1. **Engine-side lookup function.** A `Player_FindByCharID`-style function almost certainly exists (the engine binds GameConnection ↔ Player ↔ CmCharacterInfo at spawn-time). Locate it via Ghidra by searching for callers of `Character_GetByID` (RVA `0x28BC20`) and adjacent code paths that construct Player.
2. **Spawn-time hook.** Install a Detours hook on `Player::onAdd` or `GameConnection::setControlObject` (RVA visible in the engine log: `setControlObject() -- set controlling client -- ch_1[1829] 1831`) and cache the resulting `charID → Player*` mapping in a LiFx-side map. Subsequent commands look up by charID.
3. **Iterate Sim's object dictionary** via TorqueScript: enumerate every Player, match `getCharacterId()`, return its handle. Already used by the existing `Lifx::applyDamage` script path (see `lifx_character.cpp`).

Option 3 is the lowest-effort follow-up (zero new C++, just a script wrapper that hands the Player handle to a `Lifx::snapshotPlayerByObj` command). Option 2 is the cleanest long-term but adds another hook to maintain. Option 1 is the most invasive.

**Status: investigation paused.** All probes committed in PR #31. The remaining gap to "editable resurrection-sickness duration" is at least two more focused PRs (locate Player; identify the duration field; install the writer-hook), each non-trivial.

## Resolution (Session 3): active-effect table located on Player, duration editable server-side

After re-running the runtime probes against the Player ShapeBase (reached via the existing TorqueScript pattern `ClientGroup.getObject(N).getControlObject()` — no Sim-lookup RE needed; we just register C++ commands under the `Player` Torque namespace so the engine passes the Player* directly as `obj`), we found:

### Table A — Player active-effect table

- Base offset: **Player + 0x1238**
- Row stride: **24 bytes**, indexed by effect ID

Row layout, verified empirically:

| Field offset | Type | Meaning |
|---|---|---|
| row+0 | uint32 | **`expires_at_ms`** — absolute global-clock value when the effect expires |
| row+4 | uint32 | `applied_at_ms` — absolute global-clock value when the effect was applied |
| row+8 | uint32 | effect ID (self-reference matching the row index) |
| row+12 | uint32 | metadata (UI/flag state) |
| row+16 | uint32 | magnitude × 1e6 (active intensity; 0 when inactive) |
| row+20 | uint32 | unused |

Confirmation: a fresh Resurrected (effect 47) right after respawn showed `expires_at = 793,897`, `applied_at = 193,897`, **difference = 600,000 ms = exactly 10 minutes** — the documented duration of resurrection sickness. ✓

### Server-side writes work

Poking `+0x16A0` (= Table A row 47 expires_at) to a value before `applied_at` caused the **HP_MAX penalty and regen reductions to disappear server-side** — proof that the engine reads this field on each expiry tick. The client-side timer/icon stayed visible because the client computes its countdown locally from `(applied_at, duration)` sent once at apply time; updating the server's `expires_at` doesn't trigger a re-broadcast.

### Shipping API

`source/server/api/lifx_effects.cpp` now exposes three `Player`-namespace methods (callable as `%p.method()`, where `%p` is obtained by `LiFxUtility::getPlayer(charID).getControlObject()`):

| Method | Purpose |
|---|---|
| `%p.lifxSetEffectExpiry(effectID, expires_at_ms)` | Write absolute expires_at directly. |
| `%p.lifxExtendEffect(effectID, extra_ms)` | Add `extra_ms` to the current expires_at. |
| `%p.lifxClearEffect(effectID)` | Set expires_at = applied_at → effect immediately expires server-side. |

All three validate the row's self-ID stamp before writing — if the row isn't where we expect (engine version differs), they refuse rather than corrupt memory.

### Known limitations

Three behaviors observed in vivo that the current API doesn't address:

1. **Client UI HUD doesn't refresh after a write.** The Resurrected icon and countdown remain visible on the client even after a clean `lifxClearEffect`. Server-side gameplay (HP modifiers, regen rates, magnitude application) IS correct. To make the UI catch up, we'd need a separate `AddEffect`/`RemoveEffect` net-event sender — `Character_SendChanges` (RVA `0x1BC3D0`) tested with `mask=0xFFFFFFFF` updates HP/stat fields but doesn't touch the effects panel.

2. **Stat-recompute lag.** After a clear, the magnitude penalty (e.g. Resurrected's HP_MAX -10%) sometimes persists for a tick or requires a second `lifxClearEffect` call before the engine drops it. Server-side `expires_at` IS authoritative — the user observed HP regen rate change in response to the writes — but the engine appears to cache a derived stat snapshot that isn't immediately invalidated by zeroing Table A.

3. **Secondary effect storage exists but couldn't be localized.** `lifxFindEffect(47)` on a fresh post-respawn Player returned only one inline hit (`+0x16A8`), confirming Table A holds the only literal-ID reference. But the residual penalty after clear strongly suggests the engine maintains a second structure (an "active effects manager" sub-object holding pointers to Table A rows, plus pre-computed stat contributions). Finding it is the natural follow-up: snapshot Player on a wider range, watch for byte mutations on apply that aren't in Table A.

### Follow-up work (separate issues)

- **Locate the `AddEffect`/`RemoveEffect` net event sender** so client UI stays in sync with server state.
- **Locate the "active effects manager" sub-object** so clears are atomic without the recompute lag.
- **Optional: wrap as TorqueScript helpers.** A `LiFxUtility::setEffectExpiry(charID, effectID, ms)` global function combining `getPlayer(charID).getControlObject().lifxSetEffectExpiry(effectID, ms)` would make server-side scripts ergonomic.

## Worked example: adding a new effect

A new effect can be added **purely by editing data files**, without any C++ change or new hook — the engine's parser (the function we identified at RVA `0x4DD100`) will read the new entry on next server boot, and the existing item / random-event / ability machinery will then be able to apply it. The only hard constraint is that you must reuse one of the parameter-type tokens enumerated in the parser; you cannot invent a new one without further engine RE.

This walkthrough adds a fictitious effect "Lucky Streak" (+15% luck while equipped) and ties it to a new equippable trinket.

### Step 1 — pick a free effect ID

`data/cm_effects.xml` ships with IDs 1–91 and 93 in use. ID 92 is the obvious gap; anything ≥100 is also free. We'll use **`id="100"`**.

### Step 2 — define the effect in `data/cm_effects.xml`

Append before the closing `</effects>`:

```xml
<effect id="100" name="Lucky Streak">
    <flags CanMove="TRUE" CanRun="TRUE" CanJump="TRUE" CanRotate="TRUE"
           CanRotateHead="TRUE" CanOperate="TRUE" CanBeCanceled="TRUE"
           RemovedOnDeath="TRUE" />
    <description>9100</description>
    <parameter type="LUCK" applytype="INCREASE_COEFF" />
    <icon>art/2D/Effects/Luck.png</icon>
</effect>
```

Field-by-field:

| Field | Why this value |
|---|---|
| `id="100"` | Free, well above the engine's range. |
| `name="Lucky Streak"` | Internal identifier; appears in some logs. |
| `flags` | Standard "no movement restriction, can be dispelled, gone on death" — copied from ID 90 ("Luck: God's Love") which is the closest existing analogue. |
| `<description>` | Points at a `<string>` ID in `data/cm_messages.xml`. We'll add it in step 3. |
| `<parameter>` | The buff math: `LUCK` is a known parameter type; `INCREASE_COEFF` is a known applytype (multiplicative percentage increase). Both appear in the parser's fan-in (see the table at the top of this doc). |
| `<icon>` | Re-uses the existing luck icon. Adding a custom PNG is optional; the engine will fall back to a placeholder if missing. |

**Validating the parameter tuple:** the parser at `0x4DD100` is the gatekeeper. Any combination of `type=` and `applytype=` whose tokens both appear in the fan-in table is parsed successfully. The combination `(LUCK, INCREASE_COEFF)` is already used by ID 90 in the shipping data — guaranteed to parse.

### Step 3 — add the description string to `data/cm_messages.xml`

The message-ID space in the shipping file goes well past 4910. We'll use **`id="9100"`** to mirror the effect ID:

```xml
<string id="9100">Luck is on your side. (+15% Luck)</string>
```

This is what the player sees in their effect tooltip. Format-string tokens (`%d`, `%s`, …) are supported here if you want runtime-substituted text — the engine fills them from the effect's stack of magnitude args.

### Step 4 — hand the effect to a character

There are three idiomatic application paths the shipping data already uses; pick whichever fits your gameplay:

**Path A — equipment buff (recommended for this example).** Add an entry to `data/item_effects.xml` that applies the effect when a specific item is equipped. Re-using an existing item (e.g. item ID 491, "Exceptional Gold and Silver Amulet") for the demo:

```xml
<item id="491">
    <effect id="100">1 15.0</effect>   <!-- new -->
    <effect id="26">6 30.0</effect>    <!-- keep the original buff too -->
</item>
```

The two whitespace-separated numbers after `id="100"` are the magnitude args. Their meaning depends on the effect: for `INCREASE_COEFF` effects in shipping data, the first arg is a "tier" indicator and the second is the percentage. We use `15.0` to match the doc claim above.

**Path B — random event.** Add a `<wound>` or equivalent effect element to a `<event>` in `data/cm_random_events.xml` so the effect fires on `onWalk` / `onAbility` triggers.

**Path C — ability outcome.** Wire the effect into the ability's per-outcome XML (varies by ability subsystem; needs the ability subsystem RE which is not yet done).

For Path A, no script changes are needed — equipping the item is sufficient.

### Step 5 — verify

Restart the server. Watch the engine console for the parser-hook log lines (added in PR #22, see the "Seam A" section above):

```
[lifx-effect] parser hook attached at RVA 0x4DD100
[lifx-effect] parser entry (RVA 0x4DD100)
[lifx-effect] parser exit
```

If the lines fire without an error between them, the new `<effect id="100">` parsed cleanly. If the engine rejected it (typo, unknown parameter token), look for an error line between entry and exit — that's where the parser is bailing.

Then in-game: equip the trinket, open the character sheet / effects panel, confirm "Lucky Streak (+15% Luck)" is listed with the icon.

### What this doesn't unlock

- **New parameter *types*.** `LUCK_AND_SPEED`, `MANA`, etc. are not real engine tokens; the parser will silently drop any `<parameter>` element whose `type=` isn't in the fan-in table. Adding new types requires hooking the per-tick effect evaluator (not yet located — tracked in the "What's not yet known" table above).
- **Effects with no existing analogue applytype.** If you need a brand-new math operator (e.g. `EXPONENTIAL`), same caveat as above.
- **Triggering an effect from arbitrary C++ code.** That path is the runtime apply function we still haven't located. For now, application is data-driven (item, event, ability).

### Why this works without a hook

The Seam A hook from PR #22 is a **passthrough** — it logs entry/exit and forwards to the engine. The engine then does what it always does: walks `cm_effects.xml`, parses each `<effect>` element using the baked-in token enums, and populates its in-memory effect table. New entries in the XML are picked up identically to the shipping ones because the parser doesn't care how many entries the file has, only that each entry's tokens are recognised. The hook is what would let us *go beyond* the engine's data-driven extension surface — but for entries that fit within the existing surface, the data file itself is the API.

## Reproducing the scan

```bash
cd /home/mjoed/LifeIsFeudal/lifxpluss
~/.local/share/ghidra/support/analyzeHeadless ~/ghidra_projects LiF \
  -process ddctd_cm_yo_server.exe -noanalysis \
  -scriptPath scripts/ghidra -postScript LifxEffectsScan.java
ls /tmp/lifx_ghidra/effects_*.tsv
```

Outputs:

| File | Contents |
|---|---|
| `effects_xml_loaders.tsv` | Functions that reference effect-related XML filenames as literal strings. Empty in current run — filenames are likely assembled at runtime from a config root, so they don't appear as fixed `.rdata` strings. |
| `effects_param_strings.tsv` | Every xref from a function to a parameter-type token. 28 rows, 27 of them in `FUN_1404dd100`. |
| `effects_fan_in.tsv` | Functions ranked by how many distinct parameter tokens they reference — the effect parser is the top row by a wide margin. |
| `effects_classes.tsv` | 802 RTTI classes matching the `Effect`/`Ability`/`SpecialAttack`/`Trigger`/`Buff`/`Debuff` substrings. |
| `effects_class_vtables.tsv` | First 16 vtable slots for every class in `effects_classes.tsv`. The basis for the inheritance table above. |
