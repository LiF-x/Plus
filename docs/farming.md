---
title: Farming — harvest yield, quality and custom field crops
status: re
domain: reverse-engineering
tags: [farming, harvest, crops, weather, fertility]
related: [offsets.md, reverse_engineering.md, bloomery.md]
sources: [offsets.md, docs/evidence/field_crop_native_reference.json]
updated: 2026-11-11
---

# Farming — harvest yield, quality and custom field crops

Reverse-engineering and analysis contributed by **Pabluuz**.

Custom field-crop integration review and reference evidence contributed by
**Zbigi** (LiFXXL). The original harvest analysis below retains Pabluuz's authorship.

## Farming: `Harvest Crops` yield

The following RVAs apply to the 1.4.4.5 dedicated-server image. The native
implementation is `AbilityImp::HarvestPlant::_onDoPerform` from
`ability_harvestplant.cpp`.

| RVA / data RVA | Meaning |
| --- | --- |
| `+0x3A5030` | Start of `AbilityImp::HarvestPlant::_onDoPerform`. |
| `+0x3A527C` | Builds the weather-history window: `8 + stageBit0 + 2*stageBit1`, therefore 8 through 11 days. |
| `+0x3A52B0` | Calls the historical-weather getter for offsets `0, -1, -2, ...`. |
| `+0x3A52BF` | Counts weather enum `2` (`fair`). |
| `+0x3A52C8` | Counts weather enum `3` (`shower`). |
| `+0x3A52D9` | Calculates `fairCount / (showerCount + 1)`. |
| `+0x3A52F4` | Lower good-weather threshold: `0.5`. |
| `+0x3A5307` | Selects good-weather multiplier `2.0`; the fallback is `0.5`. |
| `+0x3A5358` | Starts the crop quantity calculation. |
| `+0x3A5390` | Calls `floorf` after adding `0.5`. |
| `+0x3A53A1` | Starts the harvested-item quality calculation. |
| `+0x3A53D6` | Calls `ceilf` for the final harvested-item quality. |
| `+0x3A5565` | Starts the configured regional-resource probability check; it can replace the item type but does not alter quantity. |
| `+0x471590` | Returns current or historical weather for the requested day offset. |
| `+0x87FB40` | Weather-name pointer table: `0=unknown`, `1=cloudy`, `2=fair`, `3=shower`, `4=snowy`. |

The exact quantity formula is:

```text
randomBase       = 2 + (rng & 1)              // 2 or 3
fertilityFactor  = 1 + (cropSubstance & 1)    // 2 fertile, 1 non-fertile
weatherRatio     = fairCount / (showerCount + 1)
weatherFactor    = 2.0 when 0.5 <= weatherRatio <= 2.0,
                   otherwise 0.5

quantity = floor(randomBase * fertilityFactor * weatherFactor + 0.5)
```

Mature crop substance IDs are paired in `cm_substances.cs`: odd IDs such as
`105` are `Fertile...Big`, while the following even IDs such as `106` are
`NonFertile...Big`. This is why the low bit used by the native formula is an
exact fertility multiplier.

The two stage bits are bits `0x20` and `0x40` in the geo level's
`LevelFlags`. Crop maintenance treats them as a two-bit substage counter,
incrementing it from 0 through 3 before changing the crop substance to its
next visible growth stage. Harvesting a mature crop therefore samples 8 to 11
weather days depending on its progress through the mature stage. The harvest
reader extracts the soil quality, substance, and these bits at
`+0x3AF9C0..+0x3AF9E6`; the crop-maintenance transition is visible at
`+0x26EF73..+0x26F021`.

Possible primary-stack quantities are:

| Soil state | Bad weather (`0.5`) | Good weather (`2.0`) |
| --- | ---: | ---: |
| Non-fertile | 1 or 2 | 4 or 6 |
| Fertile | 2 or 3 | 8 or 12 |

Thus weather is a hard fourfold switch before rounding, not a gradual bonus.
Fertility adds another exact twofold switch. Farming skill and luck are not
read by the quantity calculation; the skill configuration affects ability
duration, while the level-100 luck bonus only gives a chance to reduce farming
ability duration.

Wheat and flax also create a second stack with the same quantity and quality:
Wheat (`352`) adds Straw (`362`), and Flax stem (`361`) adds Flax Seeds
(`1030`). The other crops create only the primary stack. The ability's
configured 5% regional-resource roll changes the resource variant, not its
amount.

Harvested-item quality is separate from quantity:

```text
quality = ceil((1 - 0.2 * weatherFactor) * soilQuality
               + 20 * weatherFactor)
```

For good weather this is `ceil(0.6 * soilQuality + 40)`; for bad weather it is
`ceil(0.9 * soilQuality + 10)`. Soil quality therefore changes item quality,
not the number of items.

## Custom field crops: separate native and data requirements

**Contribution by Zbigi, 2026-11-11.** This section concerns crops sown on fields.
It reviews GreedyFox's [`cropTypes` PR #14](https://github.com/LiF-x/Plus/pull/14)
at commit `9cc6067302e4710fae5c92da6d2b5aab56152df6`, rather than assuming the
proposal is installed in the current main branch. The
[pinned implementation](https://github.com/GreediestFox/Plus/blob/9cc6067302e4710fae5c92da6d2b5aab56152df6/source/server/hooks/engine/hook_crop_types.cpp)
and its
[header](https://github.com/GreediestFox/Plus/blob/9cc6067302e4710fae5c92da6d2b5aab56152df6/source/server/hooks/engine/hook_crop_types.h)
remain the source for that proposal's behavior and attribution.

The example configuration defines these server mappings:

| Crop | Sow ability | Seed and primary harvest item | Six substance stages | Alias base |
| --- | ---: | ---: | --- | ---: |
| Rye | `360` | `3175` | `191..196` | `101` (Wheat) |
| Barley | `361` | `3176` | `201..206` | `101` (Wheat) |
| Oat | `362` | `3177` | `211..216` | `101` (Wheat) |
| Hops | `363` | `3751` | `221..226` | `161` (Grape) |
| Sugar Beet | `364` | `3752` | `231..236` | `141` (Carrot) |

These are IDs from the PR example, not a reservation of globally unused IDs.
Check them against the target mod's abilities, item definitions and substances.
`RegisterOneCrop` uses `seedItemId` for both sowing and the primary harvest
output; the proposal has no separate `harvestItemId` setting. It copies the
post-harvest substance from the corresponding mature alias record. Choosing an
alias does not define the new item's name, icon, model, recipes or uses.

The stages follow `fertileSmall + 0..5`: small fertile/non-fertile, normal
fertile/non-fertile, big fertile/non-fertile. Growth advances by `+2` between
visible stages. The PR derives the maintenance width from the highest configured
stage; all five examples require `101..236`, width `136`. Its eight native
comparisons originally use `cmp al,0x4b`, which means width **76**, not 75.
The header's earlier `W=75` wording is an off-by-one description; the
implementation uses 76. Every stage must fit in a byte.

### Client sowing is a separate integration step

The hook is server-side. A complete installation also needs matching item data,
skill/ability XML, client substance/material/ground-cover definitions and native
client support for the new abilities and their seed mapping. Copying Wheat's
XML ability block alone does not establish that native client support.

The [reference evidence](evidence/field_crop_native_reference.json) records two
distinct ability-to-seed lookup paths:

| Image | Lookup RVA | Map helper RVA | Map object RVA |
| --- | --- | --- | --- |
| Dedicated server | `0x3A9790` | `0x3A95C0` | `0xB98D10` |
| Client | `0x509530` | `0x509360` | `0x163A410` |

Both pass the ability ID as a map key and return the record's first `u32` as
the required item. Populating the server's map does not populate the client's
map. The reviewed PR registers server abilities and server crop records; it
does not provide the corresponding client changes. Validate the client's actual
ability registration and returned seed ID before treating the XML as sufficient.
Keep any implementation consistent with Plus's in-memory patching conventions.

`maxGatherableType` extends a different namespace: gatherable types, not terrain
substances or inventory item IDs. In this proposal `218` leaves the vanilla limit
unchanged, `219` is the none sentinel, and a raised limit is `220..254`. The PR
filters its `isValid` hook to keep `219` invalid but its three widened server
accessors accept it. Matching client gatherable handling must be considered
separately; a successful server hook attach does not establish client acceptance.

### Harvestable does not automatically mean animal feed

The unmodified reference server's feed predicate at `0x1B0E70..0x1B0EA0` reads
the item type ID from `[RDX+8]` and accepts only:

```text
352, 354, 355, 356, 358, 360, 362, 375, 1026, 1027, 1030
```

Its first branch applies bit mask `0x80055D` to IDs `352..375`; the remaining
branches accept `1026`, `1027` and `1030`. None of the five example item IDs
(`3175`, `3176`, `3177`, `3751`, `3752`) passes this predicate. A vegetable or
grain parent category does not change this ID-only check. This is a property
of the unmodified reference function, not a claim about LiFXXL's deployed hooks.

The reviewed `cropTypes` code does not extend that feed filter. If feed use is
intended, review the client container filter and server consumption path as a
separate feature. This result does not establish anything about animal taming,
which is a different ability and food-selection path.

## Status & provenance of the custom-crop contribution

**Zbigi confirms that LiFXXL operates live servers with players and its
documentation draws on real server tests.** The contribution above brings that
project experience into a review of a particular Plus proposal. It does not
classify LiFXXL or its documentation as offline.

For this review, the original server and client hashes, three short native code
paths, and the pinned PR source were inspected on 2026-11-11. The JSON contains
the exact bytes, decoded instructions, image identities and reference feed
decisions. Those decisions follow the decoded predicate; they are not a new
measurement of food consumption in a running building. No executable was modified
or launched during this inspection, and no acceptance of a Plus port is implied.

For acceptance of a particular Plus integration, record the exact DLL, client
and data versions, then check a normal player's seed selection and consumption,
growth through both soil variants, Inspect naming, mature yield and post-harvest
soil, reconnect/persistence, and the retained behavior of vanilla crops. If feed
support is added, check insertion and actual consumption separately.
