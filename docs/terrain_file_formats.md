---
title: Terrain data files - reading and preserving the binary layout
status: re
domain: reverse-engineering
tags: [terrain, binary-format, geology, serialization, validation]
author: Zbigi
assistance: ChatGPT (Codex)
related: [server_clean_install.md, farming.md]
sources: [docs/evidence/terrain_file_formats_reference.json, docs/farming.md]
updated: 2026-11-12
---

# Terrain data files - reading and preserving the binary layout

These documentation updates were generated with help from ChatGPT (Codex),
in collaboration with Zbigi, from notes and analyses collected over four months
of work on LiFXXL, a community project based on a modified LiF:YO server.

This reference describes `.ter` version `9` and the inspected
compact server `.ter2idx`/`.ter2dat` pair. It covers decoding, lossless serialization
and consistency checks for terrain edits. The surface, column index and geological
layers form one related data set; successfully writing one file does not establish
that a modified world can be loaded correctly.

| File | Role |
| --- | --- |
| `t<ID>.ter` | Terrain surface heights, rendering-material indices and material names. |
| `t<ID>.ter2idx` | Header and per-cell offsets/counts into server geological records. |
| `t<ID>.ter2dat` | Server geological layer records, including underground layers. |

All numeric layouts below use **little-endian** byte order. Offsets are file-byte
offsets unless explicitly called record offsets. They describe serialized bytes,
not a compiler's default C/C++ structure alignment. Keep the file-name ID, `.ter`
header ID and `.ter2idx` header ID consistent.

## 1. Surface file: `.ter` version 9

For side length `1024`, the inspected layout is:

| Byte offset | Length | Encoding / meaning |
| --- | --- | --- |
| `0` | `1` | U8 format version: `9`. |
| `1` | `4` | U32 TerrainID. |
| `5` | `4` | U32 raster side: `1024`. |
| `9` | `4,194,304` | `1024 * 1024` IEEE-754 binary32 height samples. |
| `4,194,313` | `1,048,576` | One U8 material-table index per raster sample. |
| `5,242,889` | `4` | U32 number of material names. |
| `5,242,893` | Variable | Material names, each prefixed with a U8 byte length. |

Both rasters use `sample = y * 1024 + x`, with `0 <= x,y < 1024`.
The height byte address is `9 + 4 * sample`; the material byte address is
`4,194,313 + sample`. These are two complete rasters, not interleaved records.

Each material-name entry is a length byte followed by exactly that many name
bytes. The inspected names are ASCII, with no trailing NUL. Preserve the raw
bytes and table order. Reordering names without remapping raster indices changes
the meaning of the surface. The sample with 29 names occupies `5,243,198` bytes;
this total size is **not** a universal constant because name lengths vary.

Check that the fixed rasters fit before reading them, reject non-finite heights,
bound every name read by the remaining file length, and require the supported
profile to end at the end of the name table. Unknown versions, dimensions or
trailing sections require explicit support; never silently discard extra bytes.

An additional client surface sample contains **4,092 entries equal to `255`**
despite having only 29 material names. Preserve this observed out-of-table
sentinel; do not index the name table with it or convert it automatically to
material `0`. Its exact rendering semantics are not established here. All other
indices must resolve to the material table for this profile.

## 2. Geological index: the inspected compact `.ter2idx` profile

The header is **48 bytes**, conveniently read as twelve U32 values. The inspected
profile has `511 * 511 = 261,121` entries of 8 bytes and a total index-file length
of `48 + 261,121 * 8 = 2,089,016` bytes.

| Byte offset | Observed value | Interpretation / write rule |
| --- | --- | --- |
| `0` | `48` | Header length. |
| `4` | `2` | Storage-format revision; separate from GeoVersion. |
| `8` | Varies | GeoVersion of this terrain baseline. |
| `12` | `8` | Layout word; retain the supported profile value. |
| `16` | `8` | Layout word; retain the supported profile value. |
| `20` | `8` | Server geological record size. |
| `24` | `48` | Primary index start in this profile. |
| `28` | `2,089,016` | Layout boundary equal to the primary table end in this profile. |
| `32` | `261,121` | Primary entry count. |
| `36` | `0` | Additional layout metadata; retain it. |
| `40` | `0x01FF01FF` | Two U16 dimensions: width `511`, height `511`. |
| `44` | Varies | TerrainID. |

This table deliberately leaves some allocation-related meanings unassigned.
A matching compact baseline does not establish a universal layout for files with
additional allocation tables, spare records or a different revision. A reader
limited to this profile must report other profiles as unsupported, rather than
rewrite them using guessed header fields.

For geological cell `(x,y)`, `0 <= x,y < 511`:

```text
column             = y * 511 + x
index_entry_byte   = 48 + column * 8
index_entry        = (record_offset: U32, level_count: U32)
first_level_byte   = record_offset * 8
column_byte_length = level_count * 8
```

**The stored offset counts records, not bytes.** For example, `(offset=12,
count=3)` selects bytes `[96,120)` of `.ter2dat`. Widen arithmetic before addition
or multiplication and check `offset + count <= data_file_bytes / 8`; a wrapped
32-bit addition must not pass the check. Require a nonempty column for this
baseline profile and a data-file length divisible by 8.

In the inspected compact pair, the first offset is `0`, each subsequent offset
equals the preceding offset plus count, and the final range ends exactly at the
end of `.ter2dat`. These are **compact-profile** invariants, not proof that any
other engine-managed allocation layout is corrupt.

## 3. Server geological records: `.ter2dat`

The inspected file is a sequence of **8-byte records without a separate file
header**. A column's records are ordered from the surface downward.

| Record byte | Width | Field |
| --- | --- | --- |
| `+0` | U16 | Altitude. |
| `+2` | U8 | Geological substance ID. |
| `+3` | U8 | Level flags. |
| `+4` | U16 | Quantity. |
| `+6` | U8 | Quality as read by the native harvest path. |
| `+7` | U8 | Opaque final byte; preserve it without assigning semantics. |

The safe explicit serialization layout is `<HBBHBB`. Some raw structure views
combine the last two bytes into a U16, but this is not evidence for a 16-bit
gameplay quality field. At RVA `0x3AF977` the engine selects a level buffer at
`rsp+0x50`; at `0x3AF9C0` it reads **one byte** from `rsp+0x56` for quality.
The following reads at `0x3AF9C8` and `0x3AF9D0` select substance and flags.
See [harvest behavior](farming.md) and the attached native-byte evidence.

The checked baseline has positive, strictly descending altitudes within every
column. It has between 1 and 7 levels per column; **7 is an observation, not an
engine limit**. Multiple layers also occur in ordinary geological strata, so
`level_count > 1` does not identify a tunnel.

Preserve all layers, flags, quantities, quality and the final byte when editing
unrelated fields. Zero flags and zero final bytes in the checked baseline do not
justify clearing them in another world. Storage ranges (`0..65535` for U16,
`0..255` for U8) do not make every representable value valid game data.

## 4. Relating the surface and geological grids

The surface has `1024 * 1024` samples; geology has `511 * 511` columns. Do not
use one grid's width to index the other. In the checked data, geological cell
`(x,y)` corresponds to surface sample `(2*x+1, 2*y+1)`:

```text
surface_height(2*x+1, 2*y+1) = float32(top_level.altitude / 5)
```

The check uses game-coordinate height units, without assuming a metre conversion.
Across all 81 terrain sets, the maximum measured difference was
`0.000048828125045474735`, below a `0.001` tolerance.

This relation is a consistency check at corresponding samples. It does not
specify how the intervening surface samples should be constructed. An edit must
keep those samples and shared terrain edges geometrically consistent as well;
changing only every second sample is not a complete surface-editing procedure.

Likewise, a `.ter` rendering-material index is **not** a geological substance ID.
Resolve the surface material through its name table and the relevant game data.
Writing a substance number directly into the material raster can select an
unrelated material or an invalid entry.

## 5. Explicit decoding and lossless encoding

The following independent Python example defines byte layouts and checks an
index range. It does not write a world or modify a running server.
`<` specifies little-endian standard sizes with no implicit padding, as documented
in [Python's struct reference](https://docs.python.org/3/library/struct.html#byte-order-size-and-alignment).

```python
from struct import Struct

TER_HEADER = Struct('<BII')       # 9 bytes
IDX_HEADER = Struct('<12I')      # 48 bytes
INDEX_ENTRY = Struct('<II')      # 8 bytes
SERVER_LEVEL = Struct('<HBBHBB') # 8 bytes, retain final byte

def column_span(index_bytes, data_bytes, x, y):
    # Call only after validating the complete supported header/profile.
    if not (0 <= x < 511 and 0 <= y < 511):
        raise ValueError('cell outside geological grid')
    if len(index_bytes) != 48 + 511 * 511 * INDEX_ENTRY.size:
        raise ValueError('unsupported index length')
    if len(data_bytes) % SERVER_LEVEL.size:
        raise ValueError('partial geological record')
    offset, count = INDEX_ENTRY.unpack_from(index_bytes, 48 + (y * 511 + x) * 8)
    if count == 0 or offset + count > len(data_bytes) // 8:
        raise ValueError('invalid column range')
    return offset * 8, (offset + count) * 8

def preserve_level(record_bytes):
    # Retains all six fields, including the opaque final byte.
    return SERVER_LEVEL.pack(*SERVER_LEVEL.unpack(record_bytes))
```

For a no-change round trip, preserve float32 bits, raw name bytes, flags and
opaque fields. Do not introduce decimal-text conversion, float64 output, host
endianness, string terminators or compiler padding. A serializer should first
prove byte-for-byte identity before applying an intended edit.

If an edit changes the number of layers, rebuild the compact `.ter2dat` and
recompute every affected index offset/count together. Do not insert bytes into
the data file while leaving subsequent offsets unchanged. Maintain layer order
and surface/top-level agreement, then reopen the resulting set and validate it.

## 6. Saving a consistent terrain set

1. Take a consistent source snapshot and retain the matching baseline metadata
   and persistent world state. Perform decoding and edits on separate copies.
2. Validate the supported profile, IDs, bounds, material table, full column
   stacks and surface/geology agreement before changing data.
3. Serialize to staging files. Reopen them, repeat the validation, and compare
   untouched bytes/fields with the source. A changed layer count requires the
   matching index rewrite described above.
4. Treat all three files as one replacement set. Flush and close the staged
   files before an operator-controlled replacement with concurrent writers
   stopped. Three individual atomic renames do not form a three-file transaction;
   retain a recoverable complete previous set.
5. For an existing world, reconcile its persistent terrain changes and baseline
   versions through a tested migration procedure. Changing the header's
   GeoVersion, resetting it to `1`, or deleting saved changes does not by itself
   migrate a world. TerrainID changes also affect references outside these files.
6. Validate the resulting terrain in the intended server/client setup, including
   terrain seams, collision, underground geometry and dependent navigation data.
   File-format validation alone cannot establish those runtime results.

Client `.ter2idxcl`/`.ter2datcl` files use a different layer representation. The
inspected client pair has 4-byte surface records and one record per column;
it does not contain the server's complete 8-byte underground layer stacks.
Renaming client files to server extensions is not a conversion.

For the vanilla installation procedure, see
[Clean dedicated-server install](server_clean_install.md). The sequence above
describes data-integrity requirements, not automatic live replacement commands.

## Status & provenance

[The reference record](evidence/terrain_file_formats_reference.json) contains
layouts, representative headers, native instruction bytes, aggregate checks and
the precise validation scope. It includes no complete terrain files or individual
terrain-file hashes.

The native observations use `ddctd_cm_yo_server.exe` version `1.4.4.5`, size
`12,441,032` bytes, image base `0x140000000`, SHA-256
`ACD99BD1A785D95B06494CBADE51A2C14EE60A521D0E88A4BE6AE1B3B685219F`.
At `0x4BD3FA` and `0x4BD405`, the data-column reader multiplies count and offset
by 8 before its read and seek operations. The quality-width observation is the
separate native harvest read described above.

The read-only asset check covered **81 terrain sets / 243 files**, terrain IDs
`406..486`, **21,150,801 index entries**, **42,318,442 geological records** and
**84,934,656 surface samples**. All checked compact ranges covered the data
exactly. One selected set (`t446`) was decoded and re-encoded into separate
scratch files; all three outputs were byte-identical to their sources.
A synthetic serialization specimen also preserved a nonzero opaque final byte.

These checks describe file inspection and serialization validation. They do not
reclassify the operating project as a simulation or establish a new in-game
terrain-edit test. No server/client was started, restarted or deployed for this
contribution. Other header profiles, the meaning of the opaque byte, and a
general migration of already-modified worlds remain outside the proven scope.
