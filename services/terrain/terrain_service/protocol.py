"""Version-one terrain tile encoding with explicit identity, bounds and a complete-response CRC."""

import math
import struct
import zlib

import numpy as np

from .content import Revision
from .island import ROOT_WIDTH, INTERVALS, SAMPLES

HEADER = struct.Struct('<8s6I2q2I16s2d6dI52x')
MAGIC = b'OFGTRN1\0'
PAYLOAD_BYTES = SAMPLES * SAMPLES * 15
MAX_RESPONSE_BYTES = 2 << 20


def encode_tile(revision: Revision, root_x: int, root_z: int, depth: int, tile_x: int, tile_z: int) -> bytes:
    """Encode one immutable tile; all channels have identical row-major sample coordinates."""
    fields, (low, high) = revision.sample(root_x, root_z, depth, tile_x, tile_z)
    body = b''.join(field.tobytes(order='C') for field in fields)
    width = ROOT_WIDTH >> depth
    bounds = (tile_x * width, low, tile_z * width, (tile_x + 1) * width, high, (tile_z + 1) * width)
    header = HEADER.pack(MAGIC, 1, HEADER.size, len(body), INTERVALS, 1, depth, root_x, root_z, tile_x, tile_z,
                         bytes.fromhex(revision.manifest['revision']), ROOT_WIDTH,
                         revision.manifest['source_spacing'], *bounds, 0)
    data = bytearray(header + body)
    struct.pack_into('<I', data, 136, zlib.crc32(data))
    return bytes(data)


def decode_tile(data: bytes) -> tuple[tuple, list[np.ndarray]]:
    """Validate an encoded tile independently before exposing read-only channel views."""
    if len(data) != HEADER.size + PAYLOAD_BYTES:
        raise ValueError('Incorrect terrain response size')
    header = HEADER.unpack_from(data)
    magic, version, header_size, body_size, intervals, halo, depth, rx, rz, x, z, revision, root, spacing, *tail = header
    if (magic != MAGIC or version != 1 or header_size != HEADER.size or body_size != PAYLOAD_BYTES
            or intervals != INTERVALS or halo != 1 or root != ROOT_WIDTH or not revision.strip(b'\0')):
        raise ValueError('Unsupported terrain header')
    if not math.isfinite(spacing) or spacing < 1 or spacing > 512 or math.log2(spacing) % 1:
        raise ValueError('Unsupported source lattice')
    terminal = int(math.log2(ROOT_WIDTH / (INTERVALS * spacing)))
    if depth > terminal or x >= 1 << depth or z >= 1 << depth:
        raise ValueError('Invalid tile address')
    min_x, low, min_z, max_x, high, max_z, crc = tail
    width = ROOT_WIDTH >> depth
    if (not all(math.isfinite(v) for v in tail[:-1]) or low > high
            or (min_x, min_z, max_x, max_z) != (x * width, z * width, (x + 1) * width, (z + 1) * width)
            or any(data[140:HEADER.size])):
        raise ValueError('Invalid tile bounds or reserved bytes')
    body = memoryview(data)[HEADER.size:]
    checksum = zlib.crc32(data[:136])
    checksum = zlib.crc32(b'\0' * 4, checksum)
    checksum = zlib.crc32(data[140:], checksum)
    if checksum != crc:
        raise ValueError('Terrain response checksum mismatch')
    fields, offset = [], 0
    for dtype in ['<f4', '<f4', '<f4', '<u2', 'u1']:
        field = np.frombuffer(body, dtype=dtype, count=SAMPLES * SAMPLES, offset=offset).reshape(SAMPLES, SAMPLES)
        fields.append(field)
        offset += field.nbytes
    surface, bedrock, water, material, validity = fields
    if not all(np.isfinite(field).all() for field in fields[:3]) or np.any(validity & 0xF8):
        raise ValueError('Invalid terrain channel values')
    if np.any((validity & 1 != 0) & (bedrock > surface)) or np.any((validity & 2 != 0) & (water < 0)):
        raise ValueError('Invalid sediment or water depth')
    if np.any((validity & 4 != 0) & (material != 1)):
        raise ValueError('Unknown material ID')
    if (np.any((validity & 1 == 0) & (bedrock != 0)) or np.any((validity & 2 == 0) & (water != 0))
            or np.any((validity & 4 == 0) & (material != 0))):
        raise ValueError('Unknown attributes must have canonical zero values')
    core = surface[1:-1, 1:-1]
    if core.min() < low or core.max() > high:
        raise ValueError('Tile bounds exclude returned surface samples')
    return header, fields
