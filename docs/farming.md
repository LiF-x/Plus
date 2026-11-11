---
title: Farming — harvest yield, quality and custom field crops
status: re
domain: reverse-engineering
tags: [farming, harvest, crops, weather, fertility]
related: [offsets.md, reverse_engineering.md, bloomery.md]
sources: [offsets.md, docs/evidence/field_crop_native_reference.json, docs/evidence/crop_food_feed_reference.json]
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

### Make a new harvest edible by players

**Implementation guide by Zbigi.** Start with the item's `objects_types` row,
not its terrain substance ID or sow ability ID. The native **Eat** ability is
`43`. Its `skill_types.xml` inventory-item requirement is:

```xml
<ent_req type="object_type_id" parent="1">227 228 229 230 231 232</ent_req>
```

The matching requirement exists in both reference client/server XMLs and the
inspected LiFXXL merged XML. `parent="1"` permits descendants of those categories.
The server's native Eat path independently checks the same ancestry through
`0x27EB30`, at `0x370063..0x370110`. A menu entry by itself is not sufficient.
The [food/feed evidence](evidence/crop_food_feed_reference.json) records the XML
requirements, category rows and native instructions.

| Parent category | Reference name | Use when defining food |
| --- | --- | --- |
| `232` | Other | Raw edible produce, following Peas (`354`) or Carrot (`358`). |
| `231` | 1 ingredient | Food classified as a one-ingredient meal. |
| `230` | 2 ingredients | Two-ingredient meal. |
| `229` | 3 ingredients | Three-ingredient meal. |
| `228` | 4 ingredients | Four-ingredient meal. |
| `227` | 5 ingredients | Five-ingredient meal. |

For a new raw vegetable intended to be eaten directly:

1. Allocate an unused object-type ID, or edit the existing crop item rather than
   defining a second row with its ID. Keep its sow/harvest mappings consistent.
2. In that item's complete `objects_types` definition, use an appropriate edible
   parent. For the Peas/Carrot pattern, the relevant field is:

   ```xml
   <ParentID>232</ParentID>
   ```

3. Retain a valid inventory-item definition, positive `UnitWeight`, stack size,
   name/icon and translations. The parent field above is an edit to an existing
   complete row, not a complete item definition or a new `IsEdible` flag.
4. Export the corresponding server object-type/SQL data and deliver the matching
   item and ability definitions to the client through the mod's normal data
   pipeline. The existing Eat ability can handle this category; a new ability ID
   or a new item-ID-specific Eat hook is unnecessary for this standard path.
5. With a normal player who can eat, check the Eat action, consumed stack count,
   hunger/food effects and retained behavior after reconnecting. Preserve the
   normal server validation and consumption pipeline.

**Category `250` is a trap for this purpose.** Its name is “Edible harvest”, but
its ancestry is `250 -> 202 -> 16 -> 7 -> 2`; it does not descend from any of the
six Eat categories. Category `21` (“Food and drink”) alone is also not one of the
six accepted categories. In the inspected LiFXXL crop source, Broad Beans `3873`
and Turnip `3874` use `232`, while Leek `3875` uses `250`. The latter source row
does not gain direct eating through the reference category rules just because
its parent is named “Edible harvest”. This describes those definitions and the
reference path, not an observation of the deployed server's current behavior.

If an ingredient must stay under `250`, one option is a cooking recipe producing
a separate food item under an accepted category. If direct eating is required
without changing its parent, both the XML eligibility and native Eat category
handling need an explicit extension. Adding only `250` or the new item ID to the
XML leaves the native classification unchanged. Reparenting an existing item may
also change which category-based recipe requirements accept it; review those
recipes. The meal category affects native food behavior, so choose it for the
intended food rather than using the highest category merely to enable Eat.

### Add the harvest as feed for breeding buildings

**Implementation guide by Zbigi.** Keep this separate from the player's Eat
category. An item may be food, feed, both, or neither. The `cropTypes` proposal
has no animal-feed option; the following is the integration contract for an
additional native extension, not configuration already supplied by Plus.

1. Define the exact **object-type IDs** allowed as extra feed. Keep the stock
   accepted set and add only the intended new items. For example, the inspected
   LiFXXL feed extension accepts `{3873, 3874, 3875}` for its Broad Beans, Turnip
   and Leek. These are not the five IDs in the earlier `cropTypes` example.
2. Extend both the **lookup-by-ID** and **definition-predicate** paths on the
   server and client. Intercept after the item definition has been resolved;
   a missing definition must still be rejected. Preserve the native return and
   cleanup path, including the client lookup's owner/reference cleanup.
3. For an additional ID, continue to the native acceptance path. For every
   other ID, preserve the original predicate. Do not replace the filter with
   “all vegetables”, “all inventory items” or a range containing unrelated IDs.
4. Keep a positive and consistent `UnitWeight` in server and client definitions.
   Deploy matching data and native support, then verify both insertion and the
   subsequent server feeding cycle. Successful insertion alone is not a feeding
   test; server-only support also does not establish client UI acceptance.

For the exact server/client hashes recorded in the evidence:

| Image and path | Function RVA | Decision site | Native accept | Vanilla fallback |
| --- | --- | --- | --- | --- |
| Server lookup by ID | `0x1B0E10` | `0x1B0E2C` | `0x1B0E54` | `0x1B0E32` |
| Server definition predicate | `0x1B0E70` | `0x1B0E73` | `0x1B0E9E` | `0x1B0E79` |
| Client lookup by ID | `0x32A460` | `0x32A51C` | `0x32A548` | `0x32A522` |
| Client definition predicate | `0x32A550` | `0x32A553` | `0x32A57E` | `0x32A559` |

Each decision site begins with `8D 81 A0 FE FF FF` (`lea eax,[rcx-352]`), with
the resolved item-type ID in `ECX`. The client lookup's acceptance sets `BL=1`
and jumps to shared cleanup; do not replace it with an early `return true`.
These are build-specific integration points, not interchangeable patch bytes.
An implementation must preserve displaced instructions, registers, stack and
control flow, and follow Plus's in-memory patching convention. A copied `CALL`
payload cannot be treated as a `JMP` payload: their stack effects differ.

Feed quantity uses item mass. In the inspected LiFXXL definitions,
`UnitWeight` values `80`, `230` and `120` correspond to `0.08`, `0.23` and
`0.12` mass per Broad Beans, Turnip and Leek respectively. Thus 100 turnips at
weight 230 occupy `100 * 230 / 1000 = 23` feed-mass units. Do not use zero weight
or enlarge the building's generic `MaxContSize` as a substitute for feed support.

The reference breeding-building table at server RVA `0xACEB40` has five records
of `0x2C` bytes and a `0xFFFFFFFF` terminator. Its feed limit is a `u32` at
record `+0x1C`, and its animal limit is a `u32` at `+0x14`:

| Object-type ID | Breeding building | Feed-mass limit | Animal limit |
| --- | --- | ---: | ---: |
| `144` | Coop | 5 | 20 |
| `143` | Barn | 40 | 15 |
| `517` | Wooden Barn | 40 | 15 |
| `133` | Small Stable | 80 | 8 |
| `134` | Large Stable | 500 | 50 |

A storage shed, decorative coop or another mod's barn does not acquire native
breeding support from its name or appearance. Check its actual object-type ID
and implementation. Extending feed acceptance in existing breeding buildings
does not register a new building type.

The reference feeding path uses a shared food reserve. Adding an item to this
filter does not implement “this crop only for chickens” or separate diets for
each species. Species-specific feed requires a separate design that also deals
with mixed populations and leftover food after animals are changed.

For verification, test one new feed at a time with the previous food reserve
depleted and a known animal population; record item quantity, stored food,
quality and the next feeding cycle. Include a valid stock feed and a non-food
item, a full-container case, and reconnect/restart persistence during a planned
test window. Keep the existing feeding period, breeding rules and consumption
logic unless changing them is a separate requirement.

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

The eating/feed guide adds a second evidence record with six native code ranges,
the Eat requirement from three XML sources, selected item-category/source rows
and all five reference breeding-building records. The LiFXXL implementation's
feed decision sites were compared with the original binaries. The guide records
how to integrate the feature into Plus; it does not install that feature.

For acceptance of a particular Plus integration, record the exact DLL, client
and data versions, then check a normal player's seed selection and consumption,
growth through both soil variants, Inspect naming, mature yield and post-harvest
soil, reconnect/persistence, and the retained behavior of vanilla crops. If feed
support is added, check insertion and actual consumption separately.
