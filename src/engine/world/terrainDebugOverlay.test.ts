import { deepEqual, equal, notEqual, ok, throws } from "node:assert/strict";
import { vec3 } from "../math/vec3.js";
import { createTerrainGenerator } from "./terrainGenerator.js";
import {
  buildTerrainDebugOverlay,
  nextTerrainDebugOverlayState,
  TERRAIN_DEBUG_OVERLAY_MODES
} from "./terrainDebugOverlay.js";

describe("terrainDebugOverlay", () => {
  it("builds deterministic overlay pixels without WebGPU", () => {
    const terrain = createTerrainGenerator();
    const options = {
      center: vec3(64, 0, 0),
      mode: "macroElevation" as const,
      resolution: 16,
      worldSize: 64
    };

    deepEqual(
      buildTerrainDebugOverlay(terrain, options).pixels,
      buildTerrainDebugOverlay(terrain, options).pixels
    );
  });

  it("builds every debug mode with opaque varied pixels", () => {
    const terrain = createTerrainGenerator();

    for (const mode of TERRAIN_DEBUG_OVERLAY_MODES) {
      const overlay = buildTerrainDebugOverlay(terrain, {
        center: vec3(64, 0, 0),
        mode,
        resolution: 16,
        worldSize: 80
      });
      const stats = pixelStats(overlay.pixels);

      equal(overlay.width, 16);
      equal(overlay.height, 16);
      equal(stats.transparentPixels, 0);
      ok(stats.uniqueColors > 8, `${mode} should produce varied debug pixels.`);
    }
  });

  it("produces visually distinct debug modes", () => {
    const terrain = createTerrainGenerator();
    const macro = buildTerrainDebugOverlay(terrain, {
      center: vec3(64, 0, 0),
      mode: "macroElevation",
      resolution: 24,
      worldSize: 80
    });
    const density = buildTerrainDebugOverlay(terrain, {
      center: vec3(64, 0, 0),
      mode: "densitySlice",
      resolution: 24,
      worldSize: 80
    });

    notEqual(
      JSON.stringify(pixelStats(macro.pixels).meanColor),
      JSON.stringify(pixelStats(density.pixels).meanColor)
    );
  });

  it("cycles through debug overlay states", () => {
    equal(nextTerrainDebugOverlayState("off"), "macroElevation");
    equal(nextTerrainDebugOverlayState("macroElevation"), "mountainness");
    equal(nextTerrainDebugOverlayState("chunkBorders"), "off");
  });

  it("validates overlay options", () => {
    const terrain = createTerrainGenerator();

    throws(() => buildTerrainDebugOverlay(terrain, {
      center: vec3(0, 0, 0),
      mode: "macroElevation",
      resolution: 0
    }), /resolution/);
    throws(() => buildTerrainDebugOverlay(terrain, {
      center: vec3(0, 0, 0),
      mode: "macroElevation",
      worldSize: 0
    }), /worldSize/);
    throws(() => buildTerrainDebugOverlay(terrain, {
      center: vec3(0, 0, 0),
      mode: "chunkBorders",
      chunkSize: 0
    }), /chunkSize/);
  });
});

function pixelStats(pixels: Uint8ClampedArray): {
  readonly transparentPixels: number;
  readonly uniqueColors: number;
  readonly meanColor: readonly number[];
} {
  const colors = new Set<string>();
  let transparentPixels = 0;
  let sumR = 0;
  let sumG = 0;
  let sumB = 0;

  for (let offset = 0; offset < pixels.length; offset += 4) {
    const r = pixels[offset];
    const g = pixels[offset + 1];
    const b = pixels[offset + 2];
    const a = pixels[offset + 3];

    colors.add(`${r},${g},${b},${a}`);
    if (a === 0) {
      transparentPixels += 1;
    }

    sumR += r;
    sumG += g;
    sumB += b;
  }

  const pixelCount = pixels.length / 4;
  return {
    transparentPixels,
    uniqueColors: colors.size,
    meanColor: [
      Math.round(sumR / pixelCount),
      Math.round(sumG / pixelCount),
      Math.round(sumB / pixelCount)
    ]
  };
}
