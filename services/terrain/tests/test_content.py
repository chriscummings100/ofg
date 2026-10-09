"""Publication, sampling, conservative bounds and binary failure tests using actual revision files."""

import json
from dataclasses import replace
import multiprocessing
import struct
import zlib

import numpy as np
import pytest

from terrain_service.content import (Revision, adopt_revision, build_bounds, publish_revision,
                                     query_bounds, validate_name, validate_revision)
from terrain_service.island import IslandParameters, ROOT_WIDTH, generate_island
from terrain_service.protocol import HEADER, PAYLOAD_BYTES, decode_tile, encode_tile


def test_subsampled_preview_keeps_full_simulation_extrema_and_unknown_water(tmp_path):
    """An unsampled summit remains in the certificate; unknown land water is distinct from dry water."""
    raster = generate_island(IslandParameters())
    full = raster.heights.copy()
    full[255, 255] = 500
    heights = full[::2, ::2].copy()
    preview = replace(raster, heights=heights, params=raster.params.model_copy(
        update={'source_intervals':256, 'source_spacing':64}))
    channels = dict(surface=heights, bedrock=heights.copy(), water=np.zeros_like(heights),
                    material=np.full(heights.shape,2,dtype='<u2'), validity=np.full(heights.shape,5,dtype='u1'))
    revision = publish_revision(tmp_path, 'preview', preview, channels=channels,
                                source_bounds=build_bounds(full)[1:])
    source = Revision(tmp_path, 'preview', revision)
    assert heights.max() < 500
    assert source.manifest['height_max'] == 500
    rx = (raster.origin_x+255*32)//ROOT_WIDTH
    rz = (raster.origin_z+255*32)//ROOT_WIDTH
    fields, bounds = source.sample(rx,rz,0,0,0)
    assert bounds[1] >= 500
    _, decoded = decode_tile(encode_tile(source,rx,rz,0,0,0))
    unknown = decoded[4] & 2 == 0
    assert unknown.any() and np.all(decoded[2][unknown] == 0)
    assert np.any(decoded[3] == 2)


@pytest.fixture(scope='module')
def source(tmp_path_factory):
    """Publish a single real raster shared by immutable-content tests."""
    root = tmp_path_factory.mktemp('content')
    raster = generate_island(IslandParameters())
    revision = publish_revision(root, 'demo', raster)
    adopt_revision(root, 'demo', revision)
    return root, raster, Revision(root, 'demo', revision)


def test_binary_identity_channels_and_layout(source):
    """Wire sizes, CRC and typed arrays preserve the documented bare-rock fixture."""
    root, raster, revision = source
    encoded = encode_tile(revision, 0, 0, 0, 0, 0)
    header, fields = decode_tile(encoded)
    assert HEADER.size == 192 and len(encoded) == 1006407
    assert header[11].hex() == revision.manifest['revision']
    np.testing.assert_array_equal(fields[0], fields[1])
    assert np.all(fields[2] >= 0) and np.all(fields[3] == 1) and np.all(fields[4] == 7)
    assert fields[0].shape == (259, 259)
    assert not fields[0].flags.writeable


def test_same_level_and_cross_level_edges(source):
    """Canonical source sampling agrees at tile edges, aprons and coincident coarse positions."""
    revision = source[2]
    _, left = decode_tile(encode_tile(revision, -1, 0, 1, 1, 0))
    _, right = decode_tile(encode_tile(revision, 0, 0, 1, 0, 0))
    for a, b in zip(left, right):
        np.testing.assert_array_equal(a[:, -3:], b[:, :3])
    _, parent = decode_tile(encode_tile(revision, 0, 0, 0, 0, 0))
    for coarse, fine in zip(parent, right):
        np.testing.assert_array_equal(coarse[1:130, 1:130], fine[1:258:2, 1:258:2])


def test_terminal_tile_reproduces_source_grid(source):
    """Every terminal sample inside the source has exactly the corresponding canonical height."""
    _, raster, revision = source
    depth = revision.manifest['terminal_depth']
    fields, _ = revision.sample(0, 0, depth, 0, 0)
    xs = (np.arange(259) - 1) * raster.params.source_spacing - raster.origin_x
    zs = (np.arange(259) - 1) * raster.params.source_spacing - raster.origin_z
    ix, iz = xs // raster.params.source_spacing, zs // raster.params.source_spacing
    good_x, good_z = (ix >= 0) & (ix <= 512), (iz >= 0) & (iz <= 512)
    np.testing.assert_array_equal(fields[0][np.ix_(good_z, good_x)], raster.heights[np.ix_(iz[good_z], ix[good_x])])


def test_hierarchy_agrees_with_independent_source_cell_scan():
    """Random rectangle queries agree with an exhaustive bilinear-corner oracle."""
    heights = np.random.default_rng(14).normal(size=(33, 33)).astype('f4')
    levels = build_bounds(heights)
    rng = np.random.default_rng(71)
    for _ in range(100):
        x0, x1 = sorted(rng.integers(0, 32, size=2))
        z0, z1 = sorted(rng.integers(0, 32, size=2))
        values = heights[z0:z1 + 2, x0:x1 + 2]
        assert query_bounds(levels, (x0, z0, x1, z1)) == (values.min(), values.max())


def test_hidden_peak_and_valley_survive_coarse_bounds(tmp_path):
    """Coarse vertices miss off-grid features but source-derived bounds retain both extrema."""
    raster = generate_island(IslandParameters())
    raster.heights[:] = -80
    # Find positive-world source positions that do not coincide with the 512 m root tile samples.
    ix, iz = -raster.origin_x // 32 + 1, -raster.origin_z // 32 + 1
    raster.heights[iz, ix] = 150
    raster.heights[iz + 2, ix + 2] = -250
    revision = Revision(tmp_path, 'peak', publish_revision(tmp_path, 'peak', raster))
    fields, bounds = revision.sample(0, 0, 0, 0, 0)
    assert fields[0][1:-1, 1:-1].max() == -80
    assert fields[0][1:-1, 1:-1].min() == -80
    assert bounds[0] <= -250 and bounds[1] >= 150
    assert revision.sample(0, 0, 4, 0, 0)[0][0].max() == 150


@pytest.mark.parametrize('address', [(0, 0, -1, 0, 0), (0, 0, 16, 0, 0), (0, 0, 1, 2, 0),
                                     (1 << 63, 0, 0, 0, 0)])
def test_bad_addresses_fail(source, address):
    """Out-of-range addresses are errors, never substituted ocean content."""
    with pytest.raises(ValueError):
        encode_tile(source[2], *address)


def test_distant_ocean_and_exact_identity(source):
    """IDs beyond JavaScript's exact integer range survive binary encoding unchanged."""
    rx, rz = -(1 << 58), (1 << 58) + 1
    header, fields = decode_tile(encode_tile(source[2], rx, rz, 0, 0, 0))
    assert header[7:9] == (rx, rz)
    assert np.all(fields[0] == -80) and np.all(fields[2] == 80)


@pytest.mark.parametrize('offset', [0, 8, 12, 16, 20, 24, 28, 48, 56, 72, 80, 88, 96, 120, 136, 140, 193])
def test_corrupted_headers_and_payload_fail(source, offset):
    """Mutating every significant header group or body is detected by the independent decoder."""
    data = bytearray(encode_tile(source[2], 0, 0, 0, 0, 0))
    if offset == 56:
        data[56:72] = b'\0' * 16
    elif offset in [80, 96]:
        struct.pack_into('<d', data, offset, float('nan'))
    else:
        data[offset] ^= 0x40
    with pytest.raises(ValueError):
        decode_tile(bytes(data))


def test_invalid_channel_values_and_lengths_fail(source):
    """CRC-valid malformed channels are still rejected semantically."""
    original = encode_tile(source[2], 0, 0, 0, 0, 0)
    for data in [original[:-1], original + b'x']:
        with pytest.raises(ValueError):
            decode_tile(data)
    count = 259 * 259
    for offset, value in [(192, float('nan')), (192 + 4 * count, 100000), (192 + 8 * count, -1)]:
        data = bytearray(original)
        struct.pack_into('<f', data, offset, value)
        struct.pack_into('<I', data, 136, 0)
        struct.pack_into('<I', data, 136, zlib.crc32(data))
        with pytest.raises(ValueError):
            decode_tile(bytes(data))


def test_publication_cancellation_and_old_revision(source, tmp_path):
    """An interrupted publication has no visible revision; a later publication cannot mutate old bytes."""
    root, raster, old = source
    old_bytes = encode_tile(old, 0, 0, 0, 0, 0)
    cancelled = multiprocessing.get_context('spawn').Event()
    cancelled.set()
    with pytest.raises(InterruptedError):
        publish_revision(tmp_path, 'cancelled', raster, cancelled)
    assert not (tmp_path / 'cancelled' / 'latest.json').exists()
    assert all(path.name.endswith('.tmp') for path in (tmp_path / 'cancelled' / 'revisions').iterdir())
    new = publish_revision(root, 'demo', raster)
    adopt_revision(root, 'demo', new)
    assert json.loads((root / 'demo/latest.json').read_text())['revision'] == new
    assert encode_tile(old, 0, 0, 0, 0, 0) == old_bytes
    with pytest.raises(FileNotFoundError):
        adopt_revision(tmp_path, 'missing', 'a' * 32)


def test_unknown_attributes_require_zero_storage(source):
    """Unknown and known zero remain distinct, while ignored bytes cannot carry arbitrary data."""
    data = bytearray(encode_tile(source[2], 0, 0, 0, 0, 0))
    count = 259 * 259
    data[192 + count * 14] = 0
    struct.pack_into('<I', data, 136, 0)
    struct.pack_into('<I', data, 136, zlib.crc32(data))
    with pytest.raises(ValueError, match='canonical zero'):
        decode_tile(bytes(data))
    for offset in [192 + count * 4, 192 + count * 8]:
        struct.pack_into('<f', data, offset, 0)
    struct.pack_into('<H', data, 192 + count * 12, 0)
    struct.pack_into('<I', data, 136, 0)
    struct.pack_into('<I', data, 136, zlib.crc32(data))
    _, fields = decode_tile(bytes(data))
    assert fields[4][0, 0] == 0 and fields[4][0, 1] == 7


@pytest.mark.parametrize('name', ['../escape', 'CON', 'AUX', 'LPT1', 'a/b', '', 'x' * 65])
def test_path_components_are_bounded(name):
    """External identifiers cannot escape the service data root."""
    with pytest.raises(ValueError):
        validate_name(name)
    with pytest.raises(ValueError):
        validate_revision(name)
