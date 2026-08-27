---
title: Contributing to the knowledge base
status: reference
domain: conventions
tags: [docs, conventions, indexing, llms-txt, memory]
related: [README.md, conventions.md, principles.md]
sources: [conventions.md]
updated: 2026-06-26
---

# Contributing to the knowledge base

This `docs/` tree is the **single canonical knowledge base** for everything we know about the
*Life is Feudal: Your Own* (LiF:YO) dedicated server, the **LiFx** extension framework, and our
reverse-engineering of the game binaries. It is written to be read by **both humans and AI agents
(Claude)**. This page is the rulebook for keeping it that way as new knowledge is learned — read it
before adding or editing a page.

## TL;DR

- One topic → **one canonical page** under `docs/`. Don't scatter the same knowledge across notes — fold it in.
- Every page starts with **YAML front-matter** (schema below).
- After adding/renaming a page, update **both** indexes: [`README.md`](README.md) (human) and [`llms.txt`](llms.txt) (machine).
- Knowledge living in a Claude **memory file** is provisional — migrate it into a `docs/` page, then leave the memory as a short pointer.
- **Never** cite private `memory/...` paths in a page body; put corroborating *in-repo* files in `sources:`.

## The two indexes

| File | Audience | What it is |
|---|---|---|
| [`README.md`](README.md) | Humans | Categorized master index: a table per domain, status badges, a "start here" path. |
| [`llms.txt`](llms.txt) | AI agents | Dense, greppable machine index ([llms.txt convention](https://llmstxt.org)): one bullet per page with status, tags, and key symbols/RVAs. The file an agent reads **first** to find the right page. |

If you add, remove, or rename a page, it must end up in **both**. Every `*.md` under `docs/` appears in
both indexes — there are no unlisted pages.

## Front-matter schema (required on every page)

```yaml
---
title: <human title>
status: <verified | re | design | ops | reference | superseded>
domain: <operations | client | reverse-engineering | lifx-framework | design | conventions>
tags: [<3-6 short kebab-case tags>]
related: [<sibling docs/ filenames, e.g. net_events.md>]
sources: [<in-repo corroborating files: source/... paths, cm_offsets.h, sibling docs>]
updated: <YYYY-MM-DD>
---
```

`sources:` is **in-repo provenance only** (source files, `cm_offsets.h`, sibling docs). Do not list
Claude memory paths — they aren't in the repo and a public reader can't open them.

## Status legend

| Badge | `status` | Meaning |
|---|---|---|
| ✅ | `verified` | Confirmed at runtime, or directly read in engine/LiFx code. |
| 🔬 | `re` | Reverse-engineered; plausible and source-grounded but not all runtime-verified. |
| 📐 | `design` | A proposal or plan; not implemented (or only partly). |
| 🛠 | `ops` | An operational runbook (install, deploy, DB access). |
| 📖 | `reference` | Stable reference material / conventions. |
| ⚠️ | `superseded` | Kept for history; a newer page or finding replaces it (link to the replacement). |

Pick the value that reflects reality. When a page mixes verified and inferred claims, use the lower
bar (`re`) and call out what *is* verified in a closing **"Status & provenance"** section.

## Domains

`operations` · `client` · `reverse-engineering` · `lifx-framework` · `design` · `conventions`.
Plus the **public TorqueScript API** mirror under `../ghdocs/docs/` — that set is the externally
published reference (mirrored to `Rampart-Games-Limited/LiFxRampart`); index it from `README.md`,
but only document TS-exposed API there, never internals or DLL names (see [`principles.md`](principles.md)).

## Page anatomy

1. Front-matter (above).
2. `# H1 title`.
3. A one-paragraph **TL;DR**.
4. Detail sections. Keep every concrete fact **verbatim** — RVAs, file/struct offsets, class & type
   IDs, vtable slots, singleton addresses, message ids, wire-byte layouts, SQL. These numbers are the
   value; never paraphrase or drop one. Put numbers in `backticks`; cross-check RVAs against
   [`../source/server/cm_offsets.h`](../source/server/cm_offsets.h) and name the constant when one exists.
5. A closing **"Status & provenance"** section: what is runtime-verified vs. inferred.

## Adding a new page — checklist

1. **Search first.** Does a page already cover this topic? If so, *merge into it* — don't make a second page.
2. Create `docs/<topic>.md` with front-matter. Match the house style (terse, engineer-to-engineer).
3. Cross-link: add `related:` front-matter entries and reciprocal inline links between the two pages.
4. Add the page to **`README.md`** (under its domain table) and **`llms.txt`** (under its domain group).
5. If the knowledge came from a Claude memory file, replace the memory body with a one-line pointer to
   the new page (see below).
6. Verify before you commit: no invented numbers, no dropped facts, all `docs/` links resolve.

## Reconciling contradictions

When a new finding contradicts an existing page, **don't silently overwrite the history**. Update the
conclusion, mark the old claim with a clear "superseded by …" note, and set the page (or that section)
status accordingly. Future readers need to know *why* the old claim was wrong, not just that it changed.
(Example: `qt_and_https.md` — the Qt 5.15 spike disproved the original "can't reach modern HTTPS" claim.)

## The memory-mirror rule

Some conventions are mirrored as Claude **memory files** so AI contexts pick them up automatically.
The pairing is one-to-one and must be kept in sync — *updating one means updating the other*:

| Repo page | Memory mirror |
|---|---|
| `conventions.md` (hook naming) | `feedback_lifx_hook_naming.md` |
| `contributing.md` (this file) | `feedback_knowledge_base.md` |

More broadly: a Claude memory file is the **scratchpad**; this knowledge base is the **canonical
record**. When a memory file accumulates a real finding, migrate it into a `docs/` page, then shrink
the memory to a pointer like *"documented in `docs/<topic>.md` — see that page."* That keeps the
durable knowledge in the repo (versioned, human-readable, in one place) and keeps memory lean.
