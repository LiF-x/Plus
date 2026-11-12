---
title: Custom tree drops and species wood integration
author: Zbigi
assistance: ChatGPT (Codex)
status: re
domain: reverse-engineering
tags: [trees, felling, logs, woodworking, abilities, troubleshooting]
related: [offsets.md, inventory_stat_lifetime.md, principles.md]
sources: [source/server/hooks/engine/hook_tree_drop.cpp, source/server/cm_server.cpp, game/config/lifxpluss.xml, docs/evidence/tree_felling_reference.json, scripts/verify_tree_felling_reference.py]
updated: 2026-11-12
---

# Custom tree drops and species wood integration

Plus can select a different movable ObjectTypeID when an existing tree species
is felled. Its current `treeDrops` hook does **not** register new forest species,
add custom-log Saw out support, or repair client crashes. A complete species
wood feature needs the object data, both sides' action handling and output
routing to agree.

Prepared with help from ChatGPT (Codex), in collaboration with Zbigi, from
documents and analyses collected over four months of work on LiFXXL, a
community project based on a modified LiF:YO server. The original Plus felling
selector and its reverse engineering remain credited to **Pabluuz**.

## What the current Plus hook supports

Inside the existing root of `config/lifxpluss.xml`:

```xml
<treeDrops enabled="1">
    <drop tree="oak" itemId="626" />
</treeDrops>
```

This deliberately uses an existing stock type. Replace `itemId` only with an
ObjectTypeID already defined and checked for the target installation. A tree's
internal `TreeType`, its dropped object's ObjectTypeID and an ability ID are
different namespaces. This page does not allocate mod IDs; an ID from another
server's configuration is not evidence that it is available in LiFx's registry.

| `TreeType` | Accepted name | `TreeType` | Accepted name |
| ---: | --- | ---: | --- |
| 0 | apple | 6 | mulberry |
| 1 | birch | 7 | oak |
| 2 | elm | 8 | aspen |
| 3 | spruce | 9 | hazel |
| 4 | pine | 10 | juniper |
| 5 | maple | 11 | spinny |

Names are case-insensitive. The parser also accepts a numeric byte `0..255`;
this is a **parser range**, not proof that the engine registers 256 species.
The species lookup at server `0x483FA0` occurs before the drop selector.
Writing `tree="12"` cannot create a species record or make an unsupported
forest type valid. New species registration requires separate investigation
of the forest data/lookup and matching client and server behavior.

Missing/disabled sections and unlisted species preserve vanilla selection.
The hook checks the original 26 bytes at server `0x3716D0`, executes the stock
softwood/hardwood choice, overrides matching entries and returns at `0x3716EA`.
See the [offset reference](offsets.md#tree-felling-and-movable-log-creation)
for the full creation path and the historical live apple-to-chair check.
That check proves movable-object creation, not the entire woodworking chain.

The current parser reads `enabled`, `tree` and `itemId`. Fields used by the
separate LiFXXL implementation, such as `boardItemId`, `billetItemId` and
`uprootBillets`, have **no implementation in Plus**. Unknown attributes are
not rejected by this parser, so copying such XML can appear to work while
leaving outputs unchanged. `Server::Init` also currently ignores the boolean
configuration result; an error message must not be mistaken for a completed
installation. These are current code observations, not additional options.

## Adding a usable species log

1. Define a concrete object in `objects_types.xml` and the corresponding server
   data. The inspected species logs use `ParentID=622`, `IsMovableObject=1`
   and `IsContainer=0`. A species-specific name alone does not supply these
   semantics. Keep the real ObjectTypeID consistent through creation and storage.
2. Define that same ID in `cm_objects.xml`, including the intended `Complete`
   and `Damaged` shapes. Supply valid model/collision assets to the server and
   matching models, materials, textures and names to the client. Verify the
   actual delivered data, not just an authoring fragment.
3. Map the existing `TreeType` to that log with `treeDrops`. This changes the
   felling result only. It does not define the standing tree, sapling or growth
   behavior.
4. Add the needed **native log classification** on both sides. Several stock
   branches compare the literal IDs `626`/`653`; a correct `ParentID` alone
   does not update those branches. A server that accepts an action can still
   have a client whose Saw out menu rejects the custom log.
5. Route each processing result explicitly: billet (ability `37`), board (`38`),
   building log (`39`) and stump Uproot (`35`). Uproot is a separate native
   result path, not another call through the felling selector.
6. Check recipe-family compatibility and real output items. Stock `324/325`
   and `326/327` are billet/board outputs; some content packs repurpose them
   as abstract parent categories. In that case, output a concrete descendant,
   not the now non-instantiable category. Do not apply that content-specific
   transformation to an otherwise stock installation.

Preserve the log's actual ID during Inspect and Saw classification. Treating a
custom log as a wood class is a branching decision; globally replacing its ID
with `626`/`653` can also change its displayed name, saved identity or output
selection. Do not assume a species name fixes its material class: the inspected
LiFXXL content deliberately assigned Hazel differently from the native class.

These are integration requirements. The current Plus selector implements step 3;
this documentation does not import the separate woodworking implementation.

## Native seams for the remaining integration

RVAs below refer to the exact 1.4.4.5 reference binaries identified in the
[evidence record](evidence/tree_felling_reference.json). Server and client
addresses are not interchangeable.

| Side | RVA | Purpose |
| --- | --- | --- |
| Server | `0x3716D0` → `0x3716EA` | Existing Plus felling output selector. |
| Server | `0x240EA0` / `0x240EBE` | Raw movable-state function / literal log check. |
| Server | `0x39633C` | Inspect log classification; preserve the presentation ID. |
| Server | `0x39E769` | Saw log classification. |
| Server | `0x39E7D3` / `0x39E7EB` → `0x39E7FD` | Board / billet selection and result-store continuation. |
| Server | `0x39EE12` | Saw availability classification. |
| Server | `0x377805` → `0x377835` | Separate 48-byte Uproot reward block. |
| Client | `0x505862` | Saw availability literal comparison with `626`. |

Historical integration notes record two server failures worth avoiding: an
unsafe inline rewrite inside raw-state handling at `0x240EBE`, and an absolute
jump whose four-byte RIP displacement was incorrectly filled with NOP bytes.
For `FF 25 00 00 00 00` plus a 64-bit destination, those four bytes must stay
zero; padding belongs after the complete jump. These are different incidents
from the client stat-change crash below. New runtime hooks still need complete
instruction/ABI verification, not just a matching RVA from this table.

## Distinguish a missing log from a client crash

| Observation | Evidence to check | Relevant mechanism |
| --- | --- | --- |
| Client exits during/after an action, especially after inventory recreation or a stat change | Exception, stack and exact client build | [Root-inventory stat callback lifetime](inventory_stat_lifetime.md). |
| Tree disappears, but no log is created; server reports SQL `1020` | Executed creation query, rollback and object-publication result | Separate forest and movable-object transactions. |
| Log exists but Saw out is missing or rejects it | Client availability and server classifiers for the same real ID | Literal log-ID checks not covered by `treeDrops`. |
| Action completes but tree still stands | Remaining tree damage/work budget | One work cycle need not complete felling. |

In the historical 2026-09-06 production-log audit, `101` of `833` executed
log-creation calls failed with error `1020`. The forest change could already
be committed when the subsequent movable-object transaction rolled back.
Queued `DB_TRAN_ADD` text is not proof that a query executed or committed.

This was **not** a demonstrated rule that decreasing Strength destroys logs:
the same audit found `31` executed creation patches after an explicitly set
Strength DOWN direction, including two transaction windows with an actual
Strength decrease and successful log creation. Keep attribute direction,
skill direction and database transaction outcomes separate.

A repair for that transaction failure must preserve one object ID and retry
the **entire creation-plus-object-patch transaction**, only after a confirmed
rollback for the intended error. Re-running the outer felling action can repeat
forest changes; retrying only an INSERT can omit publication. The historical
LiFXXL retry has separate fixture evidence; no retry is added to Plus here.

## Gameplay acceptance

For a complete new-log integration, exercise felling, Inspect, movement/carry,
Saw out outputs, partial consumption/damaged state, Uproot and persistence
after reconnect/restart. Include a stock log and every configured custom type;
check quantities, quality and real object identity, not just the visible model.

For the stat-change crash, additionally exercise Cut Down `34/36` and, when
relevant, Lower Ground Level `7`. In the inspected data all three belong to
Artisan `1`, whose stats are Int/Str. They can still coincide with a Willpower
decrease through redistribution. Record actual Willpower before/after with
Will DOWN and the relevant stat-cap conditions. An action with unchanged
Willpower does not test that crash trigger. Use a fresh client process and
repeat after inventory recreation, with its GUI open and closed.

## Status & provenance

The standard-library verifier reads the original reference files without running
or editing them:

```text
python scripts/verify_tree_felling_reference.py --server <server-exe> --client <original-client-exe>
```

It checks full-file identity, PE RVA mapping and every recorded instruction
anchor. It does not validate an arbitrary mod pack or replay the client regression.

Plus parser/selector behavior was checked directly in repository source. Native
anchors, current skill bindings and the historical client crash signature were
rechecked on 2026-11-12. The existing client-lifetime repair passed 22 native
emulation scenarios in this review; details and limits are on its own page.
Historical production observations retain their dates and scope. LiFXXL is a
running community server project; these verification methods describe the
individual checks, not the deployment status of the whole project.

This contribution corrects the integration instructions and documents the client
repair contract. It adds no new runtime hook, edited EXE, deployment or gameplay
acceptance result.
