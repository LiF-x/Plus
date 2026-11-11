---
title: Shield durability underflow from over-100-percent protection
author: Zbigi
status: re
domain: reverse-engineering
tags: [combat, shield, durability, mastery, signedness, detours]
related: [offsets.md, character_hp.md, effects_and_abilities.md]
sources: [docs/evidence/shield_durability_reference.json, docs/evidence/shield_durability_offline.txt, docs/offsets.md]
updated: 2026-11-11
---

# Shield durability underflow from over-100-percent protection

Diagnosis, implementation experience and validation contributed by **Zbigi** (LiFXXL).

**Production context, confirmed by Zbigi on 2026-11-11:** LiFXXL operates live
servers with players, and its documentation draws on real server testing. The
isolated native regression results attached below are one specific evidence set;
they do not describe the whole project as offline. Porting the correction into
Plus and validating that implementation is a separate step.

**TL;DR.** The native shield-block calculation can produce a negative remaining-damage
factor when protection exceeds 100%. The durability calculation turns this into negative
additional wear; a downstream caller consumes its low 16 bits as an unsigned loss.
That loss can exhaust the shield's remaining durability in one hit. Clamp the negative
factor to zero **after the native shield-state builder returns and before wear is
calculated**. Normal shield wear must remain enabled. This page records the hook target
and evidence; it does not install a hook or add a Plus configuration option.

## Binary and address convention

| Property | Reference |
| --- | --- |
| Executable | `ddctd_cm_yo_server.exe`, Windows x64 |
| Product / file version | `yo_1.4.4.5` / `1.4.4.5` |
| File size | `12,441,032` bytes |
| SHA-256 | `ACD99BD1A785D95B06494CBADE51A2C14EE60A521D0E88A4BE6AE1B3B685219F` |
| Preferred image base | `0x140000000` |

This is the same reference build as the [offset encyclopedia](offsets.md).
All addresses below are **server RVAs**, not file offsets or client addresses.
Use the actual loaded image base under ASLR. The new RVAs are documentation entries;
they are not currently named `CmOffset` constants.

The [evidence record](evidence/shield_durability_reference.json) includes file offsets,
short byte sequences and decoded instructions read from the matching executable on
2026-11-11. No executable file was changed or launched during that inspection.

## Failure mechanism

The relevant input is effect `74` (`0x4A`, Shields Mastery), converted from its
fixed-point value by division by `1,000,000`. Its percentage bonus contributes to
shield effectiveness. For the active-block / incoming-type-0 branch studied here:

```text
remaining = 1 - min(sqrt(shieldParameter) / 3, 1)
                * sqrt(effectiveQuality) / 10
                * (1 + masteryBonus / 100)
```

`effectiveQuality` is the value already prepared by the caller; do not assume it is
always identical to the item's displayed quality. `shieldParameter` comes from the
item-type field `uint32[+0x14] / 1000.0` via `0x27E2E0`.

At parameter `9`, effective quality `100` and a positive mastery bonus, the result
can become negative. High enough bonuses can also trigger it below Q100. Q100 alone
does **not** prove that a shield is affected, and not every shield or hit branch was
shown to produce the failure.

The native wear routine multiplies additional wear by `remaining` and converts the
result to a signed integer. The hit receiver then uses `movzx` on the low word:

```text
extraWearSigned = -50
extraWearU16    = 65486
durability     = 20000
afterExtraWear = durability - min(durability, extraWearU16) = 0
```

The final example isolates the erroneous additional wear. Ordinary blocked-hit wear
is a separate component and is not included in `afterExtraWear`.

## Native path and hook target

| RVA / range | Observation |
| --- | --- |
| `0x918E0..0x91A43` | Caller prepares effective quality, including a limit from the item's quality at `Item + 0x20`. |
| `0x91E98..0x91EBA` | Fetches effect `0x4A`; converts the fixed-point value to the mastery bonus. |
| `0x27E2E0` | Reads the item-type shield parameter and divides by `1000.0`; passed in `XMM2`. |
| `0x91F30` | Calls the shield-state builder at `0xA7180`. |
| `0xA7180..0xA7374` | Builds the shield-block state. Proposed Detours entry is `0xA7180`. |
| `0xA71CA` | Initializes effectiveness to `1.0` for the studied active-block / type-0 branch. |
| `0xA7207..0xA721B` | Applies the percentage mastery bonus. |
| `0xA7222..0xA726F` | Scales by shield parameter and effective quality, then computes `1 - effectiveness`. |
| `0xA731B` | Stores the potentially negative `float` at output state `+0x38`. |
| `0x91F35` | After the builder returns, the caller stores the blocking shield pointer at state `+0x58`. |
| `0xA5510..0xA56B6` | Native wear calculation; consumes state `+0x38` for additional wear. |
| `0xA565D..0xA5662` | `cvttss2si rcx,xmm6` creates the signed additional-wear value. |
| `0xEE224` | The hit receiver retains the blocking shield pointer in `R12`. |
| `0xEEFA1..0xEEFAE` | Loads the low 16 bits as unsigned (`movzx r8d,word ptr [rbp-0x50]`) and passes the shield from `R12`. |
| `0x1F2FBE..0x1F2FC1` | Forwards wear to the equipment container. |
| `0x29848C..0x2984B5` | Reads current durability, limits the unsigned loss to it, then subtracts. |

These observations originate in the LiFXXL analysis. The evidence JSON contains a
fresh static check of nine selected instruction sites, not a fresh execution of every
path in this table.

## Builder ABI and correction contract

The confirmed caller uses this x64 ABI (names describe the recovered roles):

```cpp
void* __fastcall BuildShieldState(
    const void* sourceHit, void* outputState,
    float shieldParameter, uint32_t effectiveQuality,
    bool flag0, bool activeBlock, bool passiveBlock, bool flag3,
    float masteryBonus);
```

`RCX` is the source hit, `RDX` the output buffer, `XMM2` the shield parameter,
and `R9D` the effective quality. Arguments 5–8 are byte-sized flags; preserve all
four. The caller places argument 9 at `[rsp+0x40]` before the call. `RAX` returns
the output buffer. Do not read the shield pointer at `+0x58` inside this builder:
the caller writes it only after returning.

The tested LiFXXL correction:

1. Call the original builder exactly once with all nine original arguments.
2. Read the returned buffer's `float` at `+0x38`.
3. If the value is less than `0.0f`, write `0.0f`; otherwise perform no write.
4. Return the original result pointer and let the native wear pipeline continue.

For an eventual Plus implementation, follow the [hook conventions](conventions.md)
and [loader rules](loader_and_injection.md). The reference entry's 25-byte prologue is:

```text
48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20
41 56 48 83 EC 70
```

These bytes identify the expected entry for validation; they are not a patch payload.
Keep the original executable on disk intact. A build mismatch needs investigation,
not reuse of this ABI on an assumed-compatible image.

Clamping also corrects the ordinary wear term's erroneous contribution above 100%
protection, since that term uses `1 - remaining`. With effective Q100, parameter `9`,
bonus `10`, base hit `50`, melee/slashing and the recorded test constants:

| Value | Native | Lower clamp |
| --- | ---: | ---: |
| Remaining factor | `-0.100000024` | `0` |
| Ordinary blocked-hit wear | `825` | `750` |
| Signed additional wear | `-50` | `0` |
| Additional wear interpreted as uint16 | `65486` | `0` |

This is not a durability-disable feature. Nonnegative cases are left untouched. Do
not apply a general unsigned-loss cap in the inventory subsystem: that would change
other items and would leave the upstream negative-damage calculation unresolved.

## Evidence and reproduction boundaries

The attached [offline result log](evidence/shield_durability_offline.txt) contains an
attribution header followed by the unchanged four-line LiFXXL harness output.
The original payload's hash is recorded in the evidence JSON. Its phrase “production detour” means the
LiFXXL hook source was compiled into the isolated harness; it does **not** mean the
Plus DLL or a live server was tested.

The historical 2026-10-18/19 tests mapped sections of the reference executable into
a private test image and called its native state-builder and wear routines. They did
not run the game's entry point, start a world, load game mods or use a database. The
imported `sqrtf` path was replaced in that private image with equivalent scalar SSE
`sqrtss`. Wear constants were explicitly set to Range `0.05`, Melee `0.15`, Armor
`0.1`, Chopping `3` and Piercing `1.2`. These are controlled test inputs, not a
measurement of a current server's runtime settings.

Recorded results:

- Initial replay: 25 quality/bonus combinations; 11 negative-factor cases.
  All 25 rows are preserved in the evidence JSON.
- Real Detours attach in the isolated harness: `82,944` native state comparisons,
  `1,728` corrected and `81,216` unchanged; `331,776` native wear comparisons.
- The matrix covers qualities `0/50/80/90/99/100`, bonuses `0/1/10/20`, parameters
  `1/4/9`, all 16 flag combinations, incoming parts `0/1/5`, damage groups `1/2/3/4`,
  weapon types `1/14/17`, damage `0/50`, and four combinations of wear flags.
- Other output-state bytes and input hit data were checked unchanged. Nonnegative
  cases retained bit-identical native wear. Detach restored the entry bytes and the
  original negative-wear regression.
- Diagnostic sampling recorded 12 lines for 1,729 corrections (the matrix plus the
  initial regression). Ordinary nonnegative blocks produced no correction records.

The low-word conversion and final subtraction were reproduced from the observed
instructions; the harness did **not** exercise the entire live inventory/SQL path.
The logs are historical evidence and were not regenerated for this documentation
change. Source filenames and SHA-256 fingerprints are recorded for provenance; the
original LiFXXL harness and hook are not shipped by this documentation contribution.

## Status & provenance

**Status: `re`.** The executable hash and selected instruction sites were rechecked
read-only on 2026-11-11. The original diagnosis, correction source and historical
harness/log agree on the mechanism. This is stronger than a formula-only guess, but
is not a Plus integration or live-game acceptance result.

For validation of a future Plus implementation, distinguish an initialization log from a correction
record that captures the actual negative factor and the builder inputs. A shield
reaching zero in an item/SQL log alone cannot distinguish this bug from ordinary wear
exceeding its remaining durability. Likewise, executing a clamp does not imply every
such hit would otherwise destroy a shield: integer truncation, hit size and current
durability still matter. A normal-player test of the eventual Plus hook remains open.
