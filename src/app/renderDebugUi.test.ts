import { equal } from "node:assert/strict";
import {
  formatOptionalNumber,
  formatOptionalRoundedNumber,
  shadowCascadeMaskFromChecks,
  terrainLodMaskToMode,
  terrainLodModeToMask
} from "./renderDebugUi.js";

describe("render debug UI helpers", () => {
  it("maps terrain LOD UI modes to Rust-owned bitmasks", () => {
    equal(terrainLodModeToMask("all"), 0xFFFFFFFF);
    equal(terrainLodModeToMask("lod0"), 0b000001);
    equal(terrainLodModeToMask("lod1"), 0b000010);
    equal(terrainLodModeToMask("lod2"), 0b000100);
    equal(terrainLodModeToMask("lod3plus"), 0xFFFFFFF8);
  });

  it("maps known terrain LOD bitmasks back to UI modes", () => {
    equal(terrainLodMaskToMode(0xFFFFFFFF), "all");
    equal(terrainLodMaskToMode(0b000001), "lod0");
    equal(terrainLodMaskToMode(0b000010), "lod1");
    equal(terrainLodMaskToMode(0b000100), "lod2");
    equal(terrainLodMaskToMode(0xFFFFFFF8), "lod3plus");
    equal(terrainLodMaskToMode(0b101010), "custom");
  });

  it("keeps the previous shadow cascade mask when all boxes are unchecked", () => {
    equal(shadowCascadeMaskFromChecks([true, false, true, false], 0b1111), 0b0101);
    equal(shadowCascadeMaskFromChecks([false, false, false, false], 0b0010), 0b0010);
  });

  it("formats dormant numeric renderer diagnostics without throwing", () => {
    equal(formatOptionalNumber(undefined as unknown as number, 1), "n/a");
    equal(formatOptionalNumber(1.25, 1), "1.3");
    equal(formatOptionalRoundedNumber(undefined as unknown as number), "n/a");
    equal(formatOptionalRoundedNumber(12.6), "13");
  });
});
