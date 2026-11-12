"""Inspect YO version-4 navmesh framing and base vertices without modifying it.

Prepared with ChatGPT (Codex), in collaboration with Zbigi.
Uses only the Python standard library. The optional terrain-footprint report
uses the centered native layout documented in navmesh_world_bounds.md.
This does not execute the game, query paths, or validate graph connectivity.
"""
import argparse
import datetime
import json
import math
import mmap
import struct
from pathlib import Path

HEADER = struct.Struct('<III5fII')
RECORD = struct.Struct('<QI4x')
MESH = struct.Struct('<15i10f')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def inspect(path, grid):
    size = path.stat().st_size
    require(size >= HEADER.size, 'Truncated navigation header')
    low, high = [math.inf] * 3, [-math.inf] * 3
    box_low, box_high = low.copy(), high.copy()
    tile_low, tile_high = [2**31] * 2, [-2**31] * 2
    polys = vertices = detail_vertices = 0
    seen, footprints = set(), set()
    grid_min = -(grid // 2) * 2044 - 1020

    with path.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        h = HEADER.unpack_from(data)
        require(h[0] == 0x4D534554 and h[1] == 4, 'Unsupported outer format')
        require(all(math.isfinite(v) for v in h[3:8]), 'Non-finite header value')
        require(h[6] > 0 and h[7] > 0 and h[8] > 0 and h[9] > 0, 'Invalid capacities')
        require(h[2] <= h[8] and 40 + h[2] * 116 <= size, 'Invalid record count or truncated file')
        position = 40
        for index in range(h[2]):
            require(position + 16 <= size, f'Truncated record {index}')
            ref, length = RECORD.unpack_from(data, position)
            position += 16
            require(ref != 0 and ref not in seen, f'Zero/duplicate tile reference at {index}')
            seen.add(ref)
            require(length >= 100 and position + length <= size, f'Invalid tile length at {index}')
            m = MESH.unpack_from(data, position)
            require(m[0] == 0x444E4156 and m[1] == 7, f'Unsupported tile format at {index}')
            require(all(v >= 0 for v in m[6:15]), f'Negative tile count at {index}')
            require(100 + m[7] * 12 <= length, f'Truncated base vertices at {index}')
            require(all(math.isfinite(v) for v in m[15:]), f'Non-finite tile parameter at {index}')
            require(all(m[18+j] <= m[21+j] for j in range(3)), f'Inverted tile bounds at {index}')
            polys += m[6]
            vertices += m[7]
            detail_vertices += m[10]
            for j in range(2):
                tile_low[j] = min(tile_low[j], m[j+2])
                tile_high[j] = max(tile_high[j], m[j+2])
            for j in range(3):
                box_low[j] = min(box_low[j], m[18+j])
                box_high[j] = max(box_high[j], m[21+j])
            raw = data[position+100:position+100+m[7]*12]
            for vertex in struct.iter_unpack('<3f', raw):
                require(all(math.isfinite(v) for v in vertex), f'Non-finite base vertex at {index}')
                for j in range(3):
                    low[j] = min(low[j], vertex[j])
                    high[j] = max(high[j], vertex[j])
                x, y = vertex[0], -vertex[2]
                column, row = math.floor((x-grid_min)/2044), math.floor((y-grid_min)/2044)
                if 0 <= column < grid and 0 <= row < grid:
                    footprints.add((column, row))
            position += length
        require(position == size, 'Unexpected trailing bytes')

    return {
        'author': 'Zbigi', 'reviewed': datetime.date.today().isoformat(),
        'cache_name': path.name, 'size_bytes': size,
        'scope': 'Complete record framing and base-vertex scan; no path query, connectivity check, detail-triangle scan or game execution.',
        'header': dict(magic=hex(h[0]), version=h[1], tile_records=h[2], origin_detour=list(h[3:6]),
                       tile_width=h[6], tile_height=h[7], max_tiles=h[8], max_polys=h[9]),
        'records_consumed': h[2], 'polygons': polys, 'base_vertices': vertices,
        'detail_vertices_declared': detail_vertices,
        'tile_coordinates_min': tile_low if h[2] else None,
        'tile_coordinates_max': tile_high if h[2] else None,
        'tile_bounds_detour': dict(min=box_low, max=box_high) if h[2] else None,
        'base_vertex_bounds_game': dict(min=[low[0], -high[2], low[1]],
                                        max=[high[0], -low[2], high[1]]) if vertices else None,
        'terrain_footprint_grid': [grid, grid],
        'terrain_footprints_with_base_vertices': len(footprints),
        'all_terrain_footprints_have_base_vertices': len(footprints) == grid * grid,
        'eof_exact': True, 'unique_nonzero_tile_references': True,
        'per_file_hashes_published': False,
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cache', type=Path)
    parser.add_argument('--grid', type=int, choices=(3, 5, 9), default=9,
                        help='Centered native terrain grid used only for footprint statistics (default: 9)')
    args = parser.parse_args()
    print(json.dumps(inspect(args.cache, args.grid), indent=2, allow_nan=False))
