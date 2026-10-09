"""Deterministic bounded Voronoi geometry and a curved, flat island on a world-aligned raster."""

from dataclasses import dataclass
import math

import numpy as np
from pydantic import BaseModel, ConfigDict, Field, model_validator

ROOT_WIDTH = 131072
INTERVALS = 256
HALO = 1
SAMPLES = INTERVALS + 1 + 2 * HALO
MASK64 = (1 << 64) - 1


class IslandParameters(BaseModel):
    """Validated physical recipe; large world identifiers are decimal strings on the wire."""

    model_config = ConfigDict(extra="forbid", allow_inf_nan=False)
    seed: str = "1"
    region_x: str = "0"
    region_z: str = "0"
    seed_spacing: int = Field(default=4096, ge=128, le=65536, strict=True)
    jitter: float = Field(default=512, ge=0)
    ocean_clearance: float = Field(default=256, ge=0)
    contour_rounding: float = Field(default=0.35, ge=0.05, le=0.45)
    contour_variation: float = Field(default=0.1, ge=0, le=0.2)
    plateau_height: float = Field(default=40, ge=-10000, le=10000)
    sea_level: float = Field(default=0, ge=-10000, le=10000)
    seabed_height: float = Field(default=-80, ge=-10000, le=10000)
    coast_width: float = Field(default=256, gt=0, le=16384)
    source_spacing: int = Field(default=32, ge=1, le=512, strict=True)
    source_intervals: int = Field(default=512, ge=256, le=2048, strict=True)

    @model_validator(mode="after")
    def validate_geometry(self):
        """Reject invalid bounds, unsupported lattices and domains too small to contain the island."""
        for text, low, high in [(self.seed, 0, MASK64), (self.region_x, -(1 << 63), (1 << 63) - 1),
                                (self.region_z, -(1 << 63), (1 << 63) - 1)]:
            if str(int(text)) != text or not low <= int(text) <= high:
                raise ValueError("World identifiers must be canonical in-range decimal strings")
        if self.jitter >= self.seed_spacing / 2:
            raise ValueError("Jitter must be less than half the seed spacing")
        if not self.seabed_height < self.sea_level < self.plateau_height:
            raise ValueError("Expected seabed < sea level < plateau")
        if self.source_spacing & (self.source_spacing - 1) or self.source_intervals & (self.source_intervals - 1):
            raise ValueError("Source spacing and interval count must be powers of two")
        radius = self.seed_spacing / math.sqrt(2) + self.jitter
        if 2 * radius > ROOT_WIDTH:
            raise ValueError("Voronoi diameter exceeds the island envelope")
        if 2 * (radius + self.coast_width + self.source_spacing) >= self.source_spacing * self.source_intervals:
            raise ValueError("Source domain must leave a seabed margin around the complete island")
        return self


def mix64(value: int) -> int:
    """Mix exact integer identifiers using fixed unsigned 64-bit arithmetic."""
    value &= MASK64
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & MASK64
    return value ^ (value >> 31)


def jitter_at(seed: int, x: int, z: int, maximum: float) -> np.ndarray:
    """Return a reproducible displacement contained in the specified radius, in metres."""
    bits = mix64(mix64(x) ^ mix64(z + 0x9E3779B97F4A7C15) ^ seed)
    angle = (bits >> 11) * (2 * math.pi / (1 << 53))
    radius = math.sqrt((mix64(bits) >> 11) / (1 << 53)) * maximum
    return np.array([math.cos(angle), math.sin(angle)]) * radius


def clip_polygon(polygon: np.ndarray, normal: np.ndarray, limit: float) -> np.ndarray:
    """Clip a convex counterclockwise polygon to dot(point, normal) <= limit."""
    output = []
    for a, b in zip(polygon, np.roll(polygon, -1, axis=0)):
        da, db = float(a @ normal - limit), float(b @ normal - limit)
        if da <= 0:
            output.append(a)
        if (da < 0 < db) or (db < 0 < da):
            output.append(a + (b - a) * (da / (da - db)))
    return np.asarray(output, dtype=np.float64).reshape(-1, 2)


def voronoi_cell(params: IslandParameters, extra_neighbours: int = 0) -> np.ndarray:
    """Construct the selected cell relative to its seed using a proven finite neighbour radius."""
    s, j = params.seed_spacing, params.jitter
    radius = s / math.sqrt(2) + j
    polygon = np.array([[-radius, -radius], [radius, -radius], [radius, radius], [-radius, radius]])
    count = math.ceil((2 * math.sqrt(2) * radius + 2 * j) / s) + extra_neighbours
    seed, rx, rz = int(params.seed), int(params.region_x), int(params.region_z)
    centre = jitter_at(seed, rx, rz, j)
    for z in range(-count, count + 1):
        for x in range(-count, count + 1):
            if x == 0 and z == 0:
                continue
            delta = np.array([x * s, z * s]) + jitter_at(seed, rx + x, rz + z, j) - centre
            polygon = clip_polygon(polygon, delta, float(delta @ delta) / 2)
    return polygon


def inset_polygon(polygon: np.ndarray, clearance: float) -> np.ndarray:
    """Offset every supporting edge inward, rejecting a collapsed ocean-clearance inset."""
    result = polygon.copy()
    for a, b in zip(polygon, np.roll(polygon, -1, axis=0)):
        edge = b - a
        outward = np.array([edge[1], -edge[0]]) / np.linalg.norm(edge)
        result = clip_polygon(result, outward, float(a @ outward) - clearance)
        if len(result) < 3:
            raise ValueError("Ocean clearance erases the island")
    return result


def flatten_cubic(points: np.ndarray, tolerance: float) -> list[np.ndarray]:
    """Subdivide a cubic until its control polygon lies within tolerance of its chord."""
    a, b, c, d = points
    chord = d - a
    length = np.linalg.norm(chord)
    if length == 0:
        return [a]
    error = max(abs(np.linalg.det(np.stack([chord, b - a]))),
                abs(np.linalg.det(np.stack([chord, c - a])))) / length
    if error <= tolerance:
        return [a]
    ab, bc, cd = (a + b) / 2, (b + c) / 2, (c + d) / 2
    abc, bcd = (ab + bc) / 2, (bc + cd) / 2
    mid = (abc + bcd) / 2
    return (flatten_cubic(np.array([a, ab, abc, mid]), tolerance)
            + flatten_cubic(np.array([mid, bcd, cd, d]), tolerance))


def curved_contour(polygon: np.ndarray, params: IslandParameters) -> tuple[np.ndarray, np.ndarray]:
    """Round each corner using shared edge handles; return cubic controls and a bounded-error polyline."""
    midpoints = (polygon + np.roll(polygon, -1, axis=0)) / 2
    handles = []
    for i, edge in enumerate(np.roll(polygon, -1, axis=0) - polygon):
        random = (mix64(int(params.seed) + i) >> 11) / (1 << 53) * 2 - 1
        factor = np.clip(params.contour_rounding * (1 + params.contour_variation * random), 0.025, 0.475)
        handles.append(edge * factor)
    cubics, points = [], []
    for i in range(len(polygon)):
        a, d = midpoints[i - 1], midpoints[i]
        curve = np.array([a, a + handles[i - 1], d - handles[i], d])
        cubics.append(curve)
        points.extend(flatten_cubic(curve, params.source_spacing / 8))
    return np.array(cubics), np.array(points)


def signed_distance(x: np.ndarray, z: np.ndarray, contour: np.ndarray) -> np.ndarray:
    """Vectorized distance to a closed polyline; positive inside by an even/odd crossing test."""
    distance2 = np.full(np.broadcast_shapes(x.shape, z.shape), np.inf)
    inside = np.zeros(distance2.shape, dtype=bool)
    for a, b in zip(contour, np.roll(contour, -1, axis=0)):
        edge = b - a
        t = np.clip(((x - a[0]) * edge[0] + (z - a[1]) * edge[1]) / (edge @ edge), 0, 1)
        distance2 = np.minimum(distance2, (x - a[0] - t * edge[0]) ** 2 + (z - a[1] - t * edge[1]) ** 2)
        if edge[1] != 0:
            inside ^= ((a[1] > z) != (b[1] > z)) & (x < a[0] + (z - a[1]) * edge[0] / edge[1])
    return np.sqrt(distance2) * np.where(inside, 1, -1)


def coastal_height(distance: np.ndarray, params: IslandParameters) -> np.ndarray:
    """Blend monotonically through sea level at the contour, with zero slope on plateau/seabed."""
    t = np.minimum(np.abs(distance) / params.coast_width, 1)
    blend = t * t * (3 - 2 * t)
    delta = np.where(distance >= 0, params.plateau_height - params.sea_level,
                     params.seabed_height - params.sea_level)
    return params.sea_level + blend * delta


@dataclass
class IslandRaster:
    """A sampled field with exact integer world origin and seed-relative diagnostic geometry."""

    heights: np.ndarray
    origin_x: int
    origin_z: int
    params: IslandParameters
    polygon: np.ndarray
    inset: np.ndarray
    curves: np.ndarray
    contour: np.ndarray
    centre_local: np.ndarray


def generate_island(params: IslandParameters, cancelled=None) -> IslandRaster:
    """Generate in bounded row strips, checking cooperative cancellation between strips."""
    polygon = voronoi_cell(params)
    inset = inset_polygon(polygon, params.ocean_clearance)
    curves, contour = curved_contour(inset, params)
    spacing, n = params.source_spacing, params.source_intervals
    jitter = jitter_at(int(params.seed), int(params.region_x), int(params.region_z), params.jitter)
    base_x, base_z = int(params.region_x) * params.seed_spacing, int(params.region_z) * params.seed_spacing
    # Subtract integer anchors before any float conversion, including beyond 2^53 metres.
    ox = (base_x // spacing + math.floor(jitter[0] / spacing) - n // 2) * spacing
    oz = (base_z // spacing + math.floor(jitter[1] / spacing) - n // 2) * spacing
    centre = np.array([base_x - ox, base_z - oz]) + jitter
    x = (np.arange(n + 1) * spacing - centre[0])[None, :]
    heights = np.empty((n + 1, n + 1), dtype='<f4')
    for row in range(0, n + 1, 32):
        if cancelled is not None and cancelled.is_set():
            raise InterruptedError("Generation cancelled")
        z = (np.arange(row, min(row + 32, n + 1)) * spacing - centre[1])[:, None]
        heights[row:row + 32] = coastal_height(signed_distance(x, z, contour), params)
    if any(np.any(edge != np.float32(params.seabed_height)) for edge in
           [heights[0], heights[-1], heights[:, 0], heights[:, -1]]):
        raise ValueError("Island does not leave a continuous seabed raster boundary")
    return IslandRaster(heights, ox, oz, params, polygon, inset, curves, contour, centre)
