import { deepEqual, equal, throws } from "node:assert/strict";
import {
  terrainVariantFieldIndex,
  updateTerrainVariantField
} from "./terrainVariantEditor.js";

const ROLLING_HILLS_VARIANT = Object.freeze([
  1, 1, 3, 16, 4, 0.004, 2, 0.5, 3, 3, 0.009, 2.1, 0.48, 1, 1.8, 2,
  0.004, 2, 0.5, 14, 0.018, 1.3, 3, 0.03, 2.05, 0.44, 3.2, 1, 1, 1, 1, 1
]);

describe("terrain variant editor helpers", () => {
  it("maps editable field ids to the Rust flat descriptor layout", () => {
    equal(terrainVariantFieldIndex("baseHeight"), 2);
    equal(terrainVariantFieldIndex("heightScale"), 3);
    equal(terrainVariantFieldIndex("ridgeHeight"), 8);
    equal(terrainVariantFieldIndex("warpAmplitude"), 19);
    equal(terrainVariantFieldIndex("detailAmplitude"), 26);
    equal(terrainVariantFieldIndex("matSnow"), 31);
  });

  it("updates one terrain variant field without mutating the source values", () => {
    const edited = updateTerrainVariantField(ROLLING_HILLS_VARIANT, "heightScale", 24.5);

    equal(ROLLING_HILLS_VARIANT[3], 16);
    equal(edited[3], 24.5);
    deepEqual(edited.slice(0, 3), ROLLING_HILLS_VARIANT.slice(0, 3));
  });

  it("rounds and clamps integer octave fields", () => {
    const edited = updateTerrainVariantField(ROLLING_HILLS_VARIANT, "ridgeOctaves", 99.2);

    equal(edited[9], 8);
  });

  it("rejects unknown fields and invalid descriptor lengths", () => {
    throws(() => terrainVariantFieldIndex("unknown"));
    throws(() => updateTerrainVariantField(ROLLING_HILLS_VARIANT.slice(0, 5), "heightScale", 2));
  });
});
