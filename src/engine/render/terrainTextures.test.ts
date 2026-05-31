import { equal, ok } from "node:assert/strict";
import { readFileSync } from "node:fs";
import {
  TERRAIN_ALBEDO_ATLAS_HEIGHT,
  TERRAIN_ALBEDO_ATLAS_TILE_COUNT,
  TERRAIN_ALBEDO_ATLAS_TILE_SIZE,
  TERRAIN_ALBEDO_ATLAS_URL,
  TERRAIN_ALBEDO_ATLAS_WIDTH
} from "./terrainTextures.js";

describe("terrainTextures", () => {
  it("points at the checked-in terrain albedo atlas", () => {
    equal(TERRAIN_ALBEDO_ATLAS_URL, "/assets/textures/terrain-albedo-atlas.png");
    equal(TERRAIN_ALBEDO_ATLAS_TILE_COUNT, 3);
    ok(TERRAIN_ALBEDO_ATLAS_TILE_SIZE >= 512);
  });

  it("keeps atlas metadata in sync with the PNG asset", () => {
    const png = readFileSync(`.${TERRAIN_ALBEDO_ATLAS_URL}`);
    const width = readPngUint32(png, 16);
    const height = readPngUint32(png, 20);

    equal(width, TERRAIN_ALBEDO_ATLAS_WIDTH);
    equal(height, TERRAIN_ALBEDO_ATLAS_HEIGHT);
    equal(width, height * TERRAIN_ALBEDO_ATLAS_TILE_COUNT);
  });
});

function readPngUint32(bytes: Uint8Array, offset: number): number {
  return (
    bytes[offset] * 0x1000000 +
    bytes[offset + 1] * 0x10000 +
    bytes[offset + 2] * 0x100 +
    bytes[offset + 3]
  );
}
