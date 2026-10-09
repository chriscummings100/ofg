"""Independent geometry and source-lattice checks for the initial island recipe."""

import multiprocessing

import numpy as np
import pytest
from pydantic import ValidationError

from terrain_service.island import (IslandParameters, coastal_height, curved_contour, generate_island,
                                    inset_polygon, signed_distance, voronoi_cell)


@pytest.mark.parametrize('changes', [dict(jitter=2048), dict(plateau_height=-1), dict(source_spacing=3),
                                    dict(source_intervals=300), dict(seed='01'), dict(region_x=str(1 << 63)),
                                    dict(seed_spacing=65536), dict(coast_width=float('nan'))])
def test_invalid_recipes_fail(changes):
    """Invalid or unrepresentable source geometry never starts generation."""
    with pytest.raises(ValidationError):
        IslandParameters(**changes)


@pytest.mark.parametrize('region', [('0', '0'), ('-31', '71'), (str(-(1 << 58)), str(1 << 59))])
def test_voronoi_matches_larger_neighbour_oracle(region):
    """A larger independent clipping neighbourhood does not alter the bounded cell."""
    p = IslandParameters(region_x=region[0], region_z=region[1])
    actual = voronoi_cell(p)
    np.testing.assert_allclose(actual, voronoi_cell(p, 6), rtol=0, atol=1e-9)
    radius = p.seed_spacing / np.sqrt(2) + p.jitter
    assert np.linalg.norm(actual, axis=1).max() <= radius + 1e-9


def test_contour_is_contained_closed_smooth_and_simple():
    """Inspect curve joins and independently check nonadjacent flattened segments for crossings."""
    p = IslandParameters()
    polygon = inset_polygon(voronoi_cell(p), p.ocean_clearance)
    curves, contour = curved_contour(polygon, p)
    for index, curve in enumerate(curves):
        following = curves[(index + 1) % len(curves)]
        np.testing.assert_array_equal(curve[-1], following[0])
        np.testing.assert_allclose(curve[-1] - curve[-2], following[1] - following[0], atol=1e-10)
    for a, b in zip(polygon, np.roll(polygon, -1, axis=0)):
        outward = np.array([b[1] - a[1], a[0] - b[0]])
        assert np.max((curves.reshape(-1, 2) - a) @ outward) <= 1e-8
    for i in range(len(contour)):
        a, b = contour[i], contour[(i + 1) % len(contour)]
        for j in range(i + 2, len(contour)):
            if i == 0 and j == len(contour) - 1:
                continue
            c, d = contour[j], contour[(j + 1) % len(contour)]
            first = np.linalg.det(np.stack([b - a, c - a])) * np.linalg.det(np.stack([b - a, d - a]))
            second = np.linalg.det(np.stack([d - c, a - c])) * np.linalg.det(np.stack([d - c, b - c]))
            assert not (first < 0 and second < 0)
    with pytest.raises(ValueError, match='erases'):
        inset_polygon(polygon, 100000)


def test_coast_crossing_plateau_and_seabed():
    """The user-facing contour denotes sea level and the profile never overshoots."""
    p = IslandParameters()
    distances = np.linspace(-2 * p.coast_width, 2 * p.coast_width, 101)
    heights = coastal_height(distances, p)
    assert np.all(np.diff(heights) >= 0)
    assert heights[0] == p.seabed_height and heights[-1] == p.plateau_height
    assert heights[50] == p.sea_level
    square = np.array([[-1, -1], [1, -1], [1, 1], [-1, 1]])
    np.testing.assert_allclose(signed_distance(np.array([0, 2]), np.array([0, 0]), square), [1, -1])


def test_raster_is_reproducible_and_world_aligned():
    """Negative/distant addressing preserves local detail without enormous float coordinates."""
    p = IslandParameters(region_x=str(-(1 << 58)), region_z='-17')
    first, second = generate_island(p), generate_island(p)
    np.testing.assert_array_equal(first.heights, second.heights)
    assert first.origin_x % p.source_spacing == first.origin_z % p.source_spacing == 0
    assert np.isfinite(first.heights).all()
    assert first.heights.min() == p.seabed_height and first.heights.max() == p.plateau_height
    cancelled = multiprocessing.get_context('spawn').Event()
    cancelled.set()
    with pytest.raises(InterruptedError):
        generate_island(p, cancelled)

