import { deepEqual, equal, ok, throws } from "node:assert/strict";
import {
  DEFAULT_TERRAIN_MATERIAL_PACK,
  TERRAIN_MATERIALS,
  packTerrainMaterialWeights,
  terrainMaterialLayer
} from "./terrainMaterials.js";

describe("terrainMaterials", () => {
  it("assigns stable 16-layer texture array indices", () => {
    equal(TERRAIN_MATERIALS.length, 16);
    equal(terrainMaterialLayer("meadowGrass"), 0);
    equal(terrainMaterialLayer("snow"), 15);
  });

  it("packs the strongest four material weights", () => {
    const packed = packTerrainMaterialWeights([
      { material: "sand", weight: 0.1 },
      { material: "cliffRock", weight: 0.5 },
      { material: "mossRock", weight: 0.2 },
      { material: "redSoil", weight: 0.15 },
      { material: "snow", weight: 0.05 }
    ]);

    deepEqual(packed.indices, [
      terrainMaterialLayer("cliffRock"),
      terrainMaterialLayer("mossRock"),
      terrainMaterialLayer("redSoil"),
      terrainMaterialLayer("sand")
    ]);
    ok(Math.abs(packed.weights.reduce((sum, weight) => sum + weight, 0) - 1) < 1e-12);
  });

  it("falls back to meadow grass when all weights are empty", () => {
    deepEqual(packTerrainMaterialWeights([]), DEFAULT_TERRAIN_MATERIAL_PACK);
    deepEqual(packTerrainMaterialWeights([{ material: "snow", weight: 0 }]), DEFAULT_TERRAIN_MATERIAL_PACK);
  });

  it("rejects unknown material layer lookups", () => {
    throws(() => terrainMaterialLayer("missing" as "meadowGrass"), /Unknown terrain material/);
  });
});
