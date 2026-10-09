"""Immutable revision storage, conservative source bounds and canonical tile sampling."""

import hashlib
import json
import math
import os
from pathlib import Path
import re
import uuid

import numpy as np
from PIL import Image, ImageDraw

from .island import IslandRaster, ROOT_WIDTH, INTERVALS, SAMPLES


def validate_name(value: str) -> str:
    """Accept one bounded path component, never traversal or platform-special path syntax."""
    if not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}", value):
        raise ValueError("Expected a 1-64 character alphanumeric island/job identifier")
    if value.upper() in {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)),
                         *(f"LPT{i}" for i in range(1, 10))}:
        raise ValueError("Reserved platform path name")
    return value


def validate_revision(value: str) -> str:
    """Require the canonical 128-bit content revision used in paths and binary headers."""
    if not re.fullmatch(r"[0-9a-f]{32}", value):
        raise ValueError("Invalid content revision")
    return value


def atomic_json(path: Path, value: dict) -> None:
    """Durably finish a JSON document before atomically replacing its destination."""
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, sort_keys=True, separators=(",", ":"), allow_nan=False)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def build_bounds(heights: np.ndarray) -> list[np.ndarray]:
    """Build min/max over bilinear source cells, then dyadic parent cells, once per publication."""
    corners = [heights[:-1, :-1], heights[1:, :-1], heights[:-1, 1:], heights[1:, 1:]]
    level = np.stack([np.minimum.reduce(corners), np.maximum.reduce(corners)])
    levels = [level]
    while level.shape[1] > 1:
        low = np.minimum.reduce([level[0, dz::2, dx::2] for dz in range(2) for dx in range(2)])
        high = np.maximum.reduce([level[1, dz::2, dx::2] for dz in range(2) for dx in range(2)])
        level = np.stack([low, high])
        levels.append(level)
    return levels


def query_bounds(levels: list[np.ndarray], rectangle: tuple[int, int, int, int]) -> tuple[float, float]:
    """Reduce only hierarchy cells overlapping an inclusive source-cell rectangle."""
    x0, z0, x1, z1 = rectangle
    low, high = math.inf, -math.inf
    stack = [(len(levels) - 1, 0, 0)]
    while stack:
        depth, x, z = stack.pop()
        width = 1 << depth
        left, top = x * width, z * width
        right, bottom = left + width - 1, top + width - 1
        if right < x0 or bottom < z0 or left > x1 or top > z1:
            continue
        if depth == 0 or (left >= x0 and top >= z0 and right <= x1 and bottom <= z1):
            low = min(low, float(levels[depth][0, z, x]))
            high = max(high, float(levels[depth][1, z, x]))
        else:
            stack.extend((depth - 1, x * 2 + dx, z * 2 + dz) for dz in range(2) for dx in range(2))
    return low, high


def save_diagnostic(path: Path, raster: IslandRaster) -> None:
    """Render a durable top-down height/contour diagnostic without application dependencies."""
    heights, p = raster.heights, raster.params
    normalized = np.clip((heights - p.seabed_height) / (p.plateau_height - p.seabed_height), 0, 1)
    land = heights > p.sea_level
    rgb = np.stack([np.where(land, 75 + normalized * 70, 20),
                    np.where(land, 100 + normalized * 80, 65 + normalized * 60),
                    np.where(land, 45 + normalized * 30, 115 + normalized * 70)], axis=-1).astype('uint8')
    image = Image.fromarray(rgb).resize((768, 768))
    draw = ImageDraw.Draw(image)
    scale = 767 / (p.source_intervals * p.source_spacing)
    for shape, color in [(raster.polygon, '#ffad42'), (raster.inset, '#b789e3'), (raster.contour, '#f4f7f9')]:
        points = [tuple(point) for point in ((shape + raster.centre_local) * scale)]
        draw.line(points + [points[0]], fill=color, width=2)
    centre = raster.centre_local * scale
    draw.ellipse((centre[0] - 3, centre[1] - 3, centre[0] + 3, centre[1] + 3), fill='red')
    draw.text((12, 12), 'OFG: Voronoi / inset / coastline', fill='white')
    image.save(path)


def publish_revision(root: Path, island: str, raster: IslandRaster, cancelled=None, *, channels=None,
                     simulation=None, drainage=None, source_bounds=None) -> str:
    """Write a complete immutable revision directory; the job owner separately adopts latest."""
    island = validate_name(island)
    revision = uuid.uuid4().hex
    parent = root / island / 'revisions'
    parent.mkdir(parents=True, exist_ok=True)
    staging = parent / (revision + '.tmp')
    staging.mkdir()
    params = raster.params
    if channels is None:
        channels = dict(surface=raster.heights, bedrock=raster.heights,
                        water=np.maximum(params.sea_level-raster.heights, 0).astype('<f4'),
                        material=np.ones(raster.heights.shape, dtype='<u2'),
                        validity=np.full(raster.heights.shape, 7, dtype='u1'))
    surface, bedrock, water, material, validity = (channels[k] for k in
                                                ['surface', 'bedrock', 'water', 'material', 'validity'])
    if (any(a.shape != raster.heights.shape for a in channels.values())
            or not np.array_equal(surface, raster.heights)
            or not all(np.isfinite(a).all() for a in [surface, bedrock, water])
            or np.any(validity & 0xf8) or np.any((validity & 1 != 0) & (bedrock > surface))
            or np.any(water < 0) or np.any((validity & 4 != 0) & ~np.isin(material, [1, 2]))
            or any(np.any((validity & bit == 0) & (values != 0))
                   for bit, values in [(1, bedrock), (2, water), (4, material)])):
        raise ValueError('Invalid published terrain channels')
    for name, dtype in [('surface','<f4'), ('bedrock','<f4'), ('water','<f4'), ('material','<u2'), ('validity','u1')]:
        np.save(staging / f'{name}.npy', channels[name].astype(dtype), allow_pickle=False)
    if drainage is not None:
        if drainage.shape != surface.shape or not np.isfinite(drainage).all() or np.any(drainage < 0):
            raise ValueError('Invalid drainage diagnostic')
        np.save(staging / 'drainage.npy', drainage, allow_pickle=False)
    levels = build_bounds(raster.heights) if source_bounds is None else source_bounds
    if (len(levels) != params.source_intervals.bit_length() or
            any(level.shape != (2, params.source_intervals >> i, params.source_intervals >> i)
                or not np.isfinite(level).all() or np.any(level[0] > level[1]) for i, level in enumerate(levels))):
        raise ValueError('Invalid conservative source hierarchy')
    for i, level in enumerate(levels):
        np.save(staging / f'bounds-{i}.npy', level, allow_pickle=False)
    manifest = {
        'format_version': 1, 'island': island, 'revision': revision, 'root_width': ROOT_WIDTH,
        'intervals': INTERVALS, 'halo': 1, 'source_spacing': params.source_spacing,
        'terminal_depth': int(math.log2(ROOT_WIDTH // (INTERVALS * params.source_spacing))),
        'source_intervals': params.source_intervals,
        'origin_x': str(raster.origin_x), 'origin_z': str(raster.origin_z),
        'origin_root_x': str(raster.origin_x // ROOT_WIDTH), 'origin_root_z': str(raster.origin_z // ROOT_WIDTH),
        'origin_local_x': raster.origin_x % ROOT_WIDTH, 'origin_local_z': raster.origin_z % ROOT_WIDTH,
        'height_min': float(min(levels[-1][0,0,0], np.float32(params.seabed_height))),
        'height_max': float(max(levels[-1][1,0,0], np.float32(params.seabed_height))),
        'sea_level': params.sea_level, 'seabed_height': float(np.float32(params.seabed_height)),
        'parameters': params.model_dump(), 'material_palette': [{'id': 1, 'name': 'bare rock'},
                                                               {'id': 2, 'name': 'sediment'}],
        'content_hash': hashlib.sha256(b''.join((staging / f'{name}.npy').read_bytes()
                                    for name in ['surface', 'bedrock', 'water', 'material', 'validity'])).hexdigest(),
        'contour_local': (raster.contour + raster.centre_local).tolist(),
        'tile_url': f'/v1/islands/{island}/revisions/{revision}/terrain/{{rootX}}/{{rootZ}}/{{dataDepth}}/{{tileX}}/{{tileZ}}.bin',
    }
    if simulation is not None:
        manifest['simulation'] = simulation
    atomic_json(staging / 'manifest.json', manifest)
    save_diagnostic(staging / 'diagnostic.png', raster)
    if cancelled is not None and cancelled.is_set():
        raise InterruptedError('Publication cancelled before commit')
    os.rename(staging, parent / revision)
    return revision


def adopt_revision(root: Path, island: str, revision: str) -> None:
    """Move latest only to an already complete revision, preserving previous immutable content."""
    source = root / validate_name(island) / 'revisions' / validate_revision(revision)
    if not (source / 'manifest.json').is_file():
        raise FileNotFoundError('Cannot publish an incomplete revision')
    atomic_json(root / island / 'latest.json', {'revision': revision})


class Revision:
    """Read-only, memory-mapped source arrays; all queries use bounded local coordinate differences."""

    def __init__(self, root: Path, island: str, revision: str):
        """Open one complete revision without importing or starting a generator."""
        self.directory = root / validate_name(island) / 'revisions' / validate_revision(revision)
        self.manifest = json.loads((self.directory / 'manifest.json').read_text(encoding='utf-8'))
        self.fields = {name: np.load(self.directory / f'{name}.npy', mmap_mode='r', allow_pickle=False)
                       for name in ['surface', 'bedrock', 'water', 'material', 'validity']}
        n = self.manifest['source_intervals']
        self.bounds = [np.load(self.directory / f'bounds-{i}.npy', mmap_mode='r', allow_pickle=False)
                       for i in range(n.bit_length())]

    def sample(self, root_x: int, root_z: int, depth: int, tile_x: int, tile_z: int):
        """Return canonical channel grids and conservative bounds for an addressed X/Z footprint."""
        m = self.manifest
        if not 0 <= depth <= m['terminal_depth'] or not 0 <= tile_x < 1 << depth or not 0 <= tile_z < 1 << depth:
            raise ValueError('Unsupported data depth or tile address')
        if not -(1 << 63) <= root_x < 1 << 63 or not -(1 << 63) <= root_z < 1 << 63:
            raise ValueError('Root address exceeds signed 64-bit range')
        width = ROOT_WIDTH >> depth
        dx = root_x * ROOT_WIDTH + tile_x * width - int(m['origin_x'])
        dz = root_z * ROOT_WIDTH + tile_z * width - int(m['origin_z'])
        n, spacing = m['source_intervals'], m['source_spacing']
        span = n * spacing
        seabed = m['seabed_height']
        # Avoid converting arbitrarily distant coordinates to floating point even for an ocean tile.
        halo = width // INTERVALS
        if dx + width + halo < 0 or dz + width + halo < 0 or dx - halo > span or dz - halo > span:
            fields = [np.full((SAMPLES, SAMPLES), v, dtype=t) for v, t in
                      [(seabed, '<f4'), (seabed, '<f4'), (max(m['sea_level'] - seabed, 0), '<f4'), (1, '<u2'), (7, 'u1')]]
            return fields, (seabed, seabed)
        x = (dx + (np.arange(SAMPLES) - 1) * halo) / spacing
        z = (dz + (np.arange(SAMPLES) - 1) * halo) / spacing
        inside = (x[None, :] >= 0) & (x[None, :] <= n) & (z[:, None] >= 0) & (z[:, None] <= n)
        x, z = np.clip(x, 0, n), np.clip(z, 0, n)
        ix, iz = np.minimum(x.astype(int), n - 1), np.minimum(z.astype(int), n - 1)
        tx, tz = x - ix, z - iz
        fields = []
        for name in ['surface', 'bedrock', 'water']:
            data = self.fields[name]
            a = data[iz[:, None], ix[None, :]].astype('f8') * (1 - tx) + data[iz[:, None], ix[None, :] + 1] * tx
            b = data[iz[:, None] + 1, ix[None, :]].astype('f8') * (1 - tx) + data[iz[:, None] + 1, ix[None, :] + 1] * tx
            values = a * (1 - tz[:, None]) + b * tz[:, None]
            outside = max(m['sea_level'] - seabed, 0) if name == 'water' else seabed
            fields.append(np.where(inside, values, outside).astype('<f4'))
        nearest_x, nearest_z = np.floor(x + .5).astype(int), np.floor(z + .5).astype(int)
        for name, outside in [('material', 1), ('validity', 7)]:
            fields.append(np.where(inside, self.fields[name][nearest_z[:, None], nearest_x[None, :]], outside))
        # A continuous attribute is known only if every sample with nonzero interpolation weight is known.
        for bit, field in [(1, fields[1]), (2, fields[2])]:
            known = np.ones(inside.shape, dtype=bool)
            for oz, wz in [(0, 1-tz), (1, tz)]:
                for ox, wx in [(0, 1-tx), (1, tx)]:
                    contributing = wz[:, None]*wx[None, :] > 0
                    values = self.fields['validity'][iz[:, None]+oz, ix[None, :]+ox]
                    known &= ~contributing | (values & bit != 0)
            known |= ~inside
            fields[4] = (fields[4] & (255-bit)) | np.where(known, bit, 0).astype('u1')
            field[~known] = 0
        low, high = seabed, seabed
        if dx <= span and dz <= span and dx + width >= 0 and dz + width >= 0:
            rectangle = (max(0, dx // spacing - 1), max(0, dz // spacing - 1),
                         min(n - 1, (dx + width) // spacing), min(n - 1, (dz + width) // spacing))
            low, high = query_bounds(self.bounds, rectangle)
            if dx < 0 or dz < 0 or dx + width > span or dz + width > span:
                low, high = min(low, seabed), max(high, seabed)
        return fields, (float(np.nextafter(low, -np.inf)), float(np.nextafter(high, np.inf)))
