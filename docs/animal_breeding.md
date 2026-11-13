---
title: Animal breeding — feeding cycles, reserves and cleanliness
author: Zbigi
assistance: ChatGPT (Codex)
status: re
domain: reverse-engineering
tags: [animals, breeding, feeding, manure, timing]
related: [farming.md, animal_data_model.md, offsets.md]
sources: [docs/evidence/animal_breeding_reference.json, docs/farming.md]
updated: 2026-11-11
---

# Animal breeding — feeding cycles, reserves and cleanliness

These documentation updates were generated with help from ChatGPT (Codex),
in collaboration with Zbigi, from notes and analyses collected over four months
of work on LiFXXL, a community project based on a modified LiF:YO server.

`animalBFPeriod` sets the breeding-cycle interval in minutes. Each cycle consumes
a species-dependent amount of food, retains unused food after successful feeding,
and runs breeding and further animal maintenance. Changing the interval changes
the frequency of this whole cycle. It does not change an animal's portion per
cycle or automatically follow `dayCycle`.

This guide concerns animals kept in breeding buildings. It complements the
[animal data model](animal_data_model.md), whose main subject is spawned animals,
and the [farming reference](farming.md). LiFXXL is a community server project with players;
the contribution draws on that experience. The accompanying
[reference evidence](evidence/animal_breeding_reference.json) identifies the
native code, definitions and calculations checked for this document.

## Reference build

All addresses are **server RVAs** for Windows x64 `ddctd_cm_yo_server.exe` 1.4.4.5,
size `12,441,032` bytes, SHA-256
`ACD99BD1A785D95B06494CBADE51A2C14EE60A521D0E88A4BE6AE1B3B685219F`.
The preferred image base is `0x140000000`; use the actual loaded base under ASLR.
These RVAs are documentation entries, not new `CmOffset` constants or hooks.

## Cycle timing and order

The world-XML reader references `animalBFPeriod` at `0x176E33`. At `0x177779`
the configured value is multiplied by `60000`, then passed to the setter at
`0x1B0960`. The setter targets `$Stable::stableTickMs`; the timer reads the
registered value at `0xACF028`.

```xml
<!-- Example: one breeding cycle per 120 configured minutes -->
<animalBFPeriod>120</animalBFPeriod>
```

This is an example, not a statement of the current LiFXXL setting or a balancing
recommendation. Historical project notes mention 60, 90 and 105 minutes at
different dates. They are different configuration snapshots, not competing
definitions of the parameter. Check the world configuration actually loaded by
the intended server; changing a template alone does not update an existing world.

`dayCycle` controls the length of the in-game day in real hours. It is a separate
setting. With a four-hour day and a 60-minute breeding period, nominally four
cycles fit in one game day. One cycle per such day would require 240 minutes;
there is no automatic coupling between these two values.

The timer at `0x1ADA80` reads the engine's simulation clock. It proceeds only
when `now > last_cycle + period`, initializes the timestamp before the first
normal cycle, and records the current timestamp after processing. Therefore
minute/day calculations below are nominal scheduling calculations, not a promise
of exact wall-clock timestamps during stalls, restarts or delayed processing.

| Step in the due-cycle driver | Call site | Target |
| --- | --- | --- |
| Feeding and food/manure accounting | `0x1ADAD6` | `0x1B29D0` |
| Breeding attempts | `0x1ADADE` | `0x1B21E0` |
| Further native animal maintenance | `0x1ADAE6` | `0x1B2FA0` |
| Refresh population food requirements | `0x1ADAEE` | `0x1B35A0` |

The driver updates its timestamp at `0x1ADBA9`. Feeding also refreshes
requirements before selecting food. The additional maintenance call is identified
here to show the full scheduling scope;
this page does not supply an unverified formula for every animal product.

Increasing the period reduces feeding and breeding opportunities per unit time.
It does not multiply the food portion to compensate. A 120-minute period gives
half the nominal cycle frequency of 60 minutes; it is not an independent
"appetite" control.

## Food demand per cycle

`0x1B35A0` resets `CmStable + 0xA0`, then visits the animal records. The callback
at `0x1B1860` reads the animal type's portion through `0x1B0450` and adds it to
that total. The native animal table starts at `0xACEC80`, with `0x34`-byte
records, object-type ID at `+0`, a `float32` portion at `+0x30`, and an
`0xFFFFFFFF` terminator.

| Animal | Object-type IDs | Food mass per animal per cycle |
| --- | --- | ---: |
| Chicken, rabbit | `1052`, `1053` | 0.03 |
| Sheep, ram | `1050`, `1051` | 0.30 |
| Pig, boar | `1048`, `1049` | 0.30 |
| Cow, bull | `1046`, `1047` | 1.20 |
| Horse, stallion | `1039`, `1040` | 1.20 |

The table contains 17 records, including other mounts with portion 1.20:
`1042`, `1044`, `1043`, `1045`, `1140`, `1663` and `1685`. A portion record
does **not** make a type eligible for every building; admission uses the separate
building table below. The evidence preserves all 17 records rather than treating
the ten common husbandry types as the complete native table.

For a fixed population, `demand = sum(number_of_animals * portion)`. Births,
removals and population changes alter that total. These are game mass units,
often displayed as stone; they are not a physical-kilogram conversion.

## Inventory food, reserve and quality are different values

A building holds both inventory stacks and a processed food reserve. Moving food
into its inventory does not mean it has already been consumed by the animals.
The feeding path selects accepted items, removes whole units from the inventory,
and adds their mass to the reserve.

| `CmStable` offset | Role in the inspected feeding path |
| --- | --- |
| `+0x34` | Food mass left in the processed reserve (`FoodLeft`). |
| `+0x38` | Quality of that reserve. |
| `+0x3C` | Accumulated manure amount. |
| `+0x40` | Manure quality. |
| `+0x44` | Food-shortage counter. |
| `+0x48` | Counter incremented when manure exceeds its limit. |
| `+0xA0` | Population food requirement for the cycle. |
| `+0xA4` | Computed dirt percentage. |

The item-weight getter at `0x27E2E0` returns `UnitWeight / 1000`. At
`0x1B2AD0..0x1B2B8B`, food selection fills the gap between the reserve and
the requirement, taking no more items than are available. Conceptually:

```text
missing_mass = max(demand - reserve_mass, 0)
items_to_take = min(available_items, ceil(missing_mass / unit_mass))
added_mass = items_to_take * unit_mass

new_quality = (old_reserve * old_quality + added_mass * item_quality)
              / (old_reserve + added_mass)
new_reserve = old_reserve + added_mass
```

The implementation uses floating-point comparisons and integer conversion, so
boundary rounding must be checked in the actual implementation rather than
assumed from decimal arithmetic. It skips a zero-weight item in this path.
The removal call is `0x1B2B87 -> 0x1A5A50`.

Quality participates in the weighted average. It does not appear as an extra
quantity multiplier in this selection calculation: low-quality carrots do not,
through this code, increase the species' required food mass.

At `0x1B2C24..0x1B2C9B`:

- With enough reserve, subtract the required mass and retain the remainder for
  later cycles. The shortage counter is cleared on the successful path.
- With insufficient reserve, set the reserve to zero and increment the shortage
  counter. The successful-feed manure increment is skipped.

The shared reserve stores mass and quality, not a separate balance for each
ingredient or animal species. A type filter alone cannot enforce separate diets
after food has entered this common reserve.

### Example: a cow and a bull eating carrots

Together they require `2 * 1.20 = 2.40` mass per successful cycle. The reference
Carrot (`358`) has `UnitWeight=230`, so one carrot supplies `0.23` mass.
Starting with no reserve, the first cycle needs 11 carrots, adds `2.53`, consumes
`2.40` and leaves approximately `0.13`. The next cycle can use this remainder;
do not independently round every cycle up to 11 and discard the surplus.

| Period in minutes | Nominal cycles / 24 h | Mass for that many cycles | Whole carrots from an empty reserve, ideal arithmetic |
| ---: | ---: | ---: | ---: |
| 60 | 24 | 57.6 | 251 |
| 90 | 16 | 38.4 | 167 |
| 120 | 12 | 28.8 | 126 |
| 240 | 6 | 14.4 | 63 |

This example assumes exactly the stated cycles, two unchanged animals, enough
carrots and no other food. Existing reserve, a different starting phase or changed
population changes the inventory count. `250` carrots weigh `57.5`; they do not
alone cover the full `57.6` mass of 24 cycles starting from an empty reserve.

## Buildings, manure and dirt

The native building table at `0xACEB40` has five `0x2C`-byte records and an
`0xFFFFFFFF` terminator. Allowed animal IDs occupy `+0x04..+0x10`; animal limit,
feed-mass limit and manure limit are unsigned integers at `+0x14`, `+0x1C` and
`+0x20` respectively. These are separate from generic object `MaxContSize`.

| Building ID | Building | Allowed animals | Animal limit | Feed-mass limit | Manure limit |
| --- | --- | --- | ---: | ---: | ---: |
| `144` | Coop | Chicken, rabbit | 20 | 5 | 3 |
| `143` | Barn | Pig, boar, sheep, ram | 15 | 40 | 20 |
| `517` | Wooden Barn | Pig, boar, sheep, ram | 15 | 40 | 20 |
| `133` | Small Stable | Cow, bull, horse, stallion | 8 | 80 | 40 |
| `134` | Large Stable | Cow, bull, horse, stallion | 50 | 500 | 250 |

These are reference-build records. A custom building's name or appearance does
not register it for husbandry. Existing mods or proposed aliases can change its
admission and capacities; use the values installed on the target server/client.
Adding a new edible crop or feed item is a separate integration task, covered by
the [custom-crop contribution](https://github.com/LiF-x/Plus/pull/19).

After successful feeding, manure increases by `0.5 * demand`. The multiplier is
the float at `0x736550`, used at `0x1B2C6F`. At `0x1B2CCA..0x1B2CEE`:

```text
dirt_percent = manure_amount / manure_limit * 100
if manure_amount > manure_limit:
    increment over-limit counter
```

The counter condition is strictly greater than the limit. This calculation does
not say that 98% dirt causes immediate death, nor does a percentage identify the
starting condition, elapsed cycles or current population. Changing the period
does not remove manure already accumulated.

For illustration, a regular 105-minute schedule can have at most five due cycles
in an eight-hour window, depending on phase. With full unchanged populations,
enough food, no cleaning and an initially clean building:

| Building | Demand / cycle | Food mass over 5 cycles | Added manure | Dirt after 5 cycles |
| --- | ---: | ---: | ---: | ---: |
| Coop | 0.60 | 3.00 | 1.50 | 50% |
| Barn / Wooden Barn | 4.50 | 22.50 | 11.25 | 56.25% |
| Small Stable | 9.60 | 48.00 | 24.00 | 60% |
| Large Stable | 60.00 | 300.00 | 150.00 | 60% |

These are planning calculations, not measurements of a particular live building.
Births, starvation, removals and pre-existing dirt change the result.

## Breeding opportunities and population growth

The feeding and breeding calls share the same due cycle. A shorter period creates
more opportunities for reproduction, while a larger population can increase both
food demand and the pool of candidates. It does not guarantee a birth from every
animal on every cycle.

The reference server script supplies these starting values:

```text
$Stable::stableTickMs      = 3600000
$Stable::BreedingPairPen   = 0.025
$Stable::BreedingNonpair   = 0.02
$Stable::BreedingPairFree  = 0.04
```

These are script defaults, not a snapshot of loaded production settings. The
world setting feeds into the timer separately. The native breeding path reads
weather and the shortage/over-limit counters, modifies rate factors at
`0x1B2353..0x1B23BB`, and checks capacity and candidate eligibility at
`0x1B240C..0x1B242C`. Do not present the three script values as unconditional
birth probabilities for a coop. Changing the timer changes opportunities;
changing a rate factor is a different balancing decision.

## Diagnose a reported feeding or cleanliness problem

Record the building type, loaded breeding period, animal counts, item quantities
and weights, food reserve and quality, manure/dirt, and the observation times.
For one observed cycle, account for feeding, births, removals, manual inventory
changes and cleaning. Compare item counts with item counts and mass with mass.

Check accepted-item insertion separately from actual consumption, and repeat the
state check after a planned reconnect/restart when investigating persistence.
A full inventory, a large processed reserve and a high dirt percentage describe
different state. One screenshot or the disappearance of some carrots does not
by itself establish a timer or consumption bug.

## Status & provenance

For this Plus contribution, the matching executable, 15 selected instruction
ranges, all 17 animal rows, all five building rows, script defaults and arithmetic
examples were checked on 2026-11-11. The evidence includes exact bytes, decoded
instructions, table layouts, source fingerprints and explicit example assumptions.

Historical configuration snapshots were reconciled instead of being presented as
current settings. This is a documentation contribution: it changes no game code,
world configuration, server process or production data. Exact birth outcomes,
product yields and failure-to-death thresholds are not inferred from the timer,
the script constants or the dirt percentage alone.
