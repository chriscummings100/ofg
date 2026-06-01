export const TERRAIN_MATERIALS = [
  {
    id: "meadowGrass",
    name: "Meadow Grass",
    albedoUrl: "/assets/textures/polyhaven/meadowGrass/albedo.jpg",
    normalUrl: "/assets/textures/polyhaven/meadowGrass/normal.jpg",
    roughnessUrl: "/assets/textures/polyhaven/meadowGrass/roughness.jpg"
  },
  {
    id: "dryGround",
    name: "Dry Ground",
    albedoUrl: "/assets/textures/polyhaven/dryGround/albedo.jpg",
    normalUrl: "/assets/textures/polyhaven/dryGround/normal.jpg",
    roughnessUrl: "/assets/textures/polyhaven/dryGround/roughness.jpg"
  },
  {
    id: "forestGround",
    name: "Forest Ground",
    albedoUrl: "/assets/textures/polyhaven/forestGround/albedo.jpg",
    normalUrl: "/assets/textures/polyhaven/forestGround/normal.jpg",
    roughnessUrl: "/assets/textures/polyhaven/forestGround/roughness.jpg"
  },
  {
    id: "leafLitter",
    name: "Leaf Litter",
    albedoUrl: "/assets/textures/polyhaven/leafLitter/albedo.jpg",
    normalUrl: "/assets/textures/polyhaven/leafLitter/normal.jpg",
    roughnessUrl: "/assets/textures/polyhaven/leafLitter/roughness.jpg"
  },
  {
    id: "bareSoil",
    name: "Bare Soil",
    albedoUrl: "/assets/textures/polyhaven/bareSoil/albedo.jpg",
    normalUrl: "/assets/textures/polyhaven/bareSoil/normal.jpg",
    roughnessUrl: "/assets/textures/polyhaven/bareSoil/roughness.jpg"
  },
  {
    id: "dryMud",
    name: "Dry Mud",
    albedoUrl: "/assets/textures/polyhaven/dryMud/albedo.jpg",
    normalUrl: "/assets/textures/polyhaven/dryMud/normal.jpg",
    roughnessUrl: "/assets/textures/polyhaven/dryMud/roughness.jpg"
  },
  {
    id: "wetMud",
    name: "Wet Mud",
    albedoUrl: "/assets/textures/polyhaven/wetMud/albedo.jpg",
    normalUrl: "/assets/textures/polyhaven/wetMud/normal.jpg",
    roughnessUrl: "/assets/textures/polyhaven/wetMud/roughness.jpg"
  },
  {
    id: "sand",
    name: "Sand",
    albedoUrl: "/assets/textures/polyhaven/sand/albedo.jpg",
    normalUrl: "/assets/textures/polyhaven/sand/normal.jpg",
    roughnessUrl: "/assets/textures/polyhaven/sand/roughness.jpg"
  },
  {
    id: "gravelSand",
    name: "Gravel Sand",
    albedoUrl: "/assets/textures/polyhaven/gravelSand/albedo.jpg",
    normalUrl: "/assets/textures/polyhaven/gravelSand/normal.jpg",
    roughnessUrl: "/assets/textures/polyhaven/gravelSand/roughness.jpg"
  },
  {
    id: "riverPebbles",
    name: "River Pebbles",
    albedoUrl: "/assets/textures/polyhaven/riverPebbles/albedo.jpg",
    normalUrl: "/assets/textures/polyhaven/riverPebbles/normal.jpg",
    roughnessUrl: "/assets/textures/polyhaven/riverPebbles/roughness.jpg"
  },
  {
    id: "scree",
    name: "Scree",
    albedoUrl: "/assets/textures/polyhaven/scree/albedo.jpg",
    normalUrl: "/assets/textures/polyhaven/scree/normal.jpg",
    roughnessUrl: "/assets/textures/polyhaven/scree/roughness.jpg"
  },
  {
    id: "rockyGround",
    name: "Rocky Ground",
    albedoUrl: "/assets/textures/polyhaven/rockyGround/albedo.jpg",
    normalUrl: "/assets/textures/polyhaven/rockyGround/normal.jpg",
    roughnessUrl: "/assets/textures/polyhaven/rockyGround/roughness.jpg"
  },
  {
    id: "cliffRock",
    name: "Cliff Rock",
    albedoUrl: "/assets/textures/polyhaven/cliffRock/albedo.jpg",
    normalUrl: "/assets/textures/polyhaven/cliffRock/normal.jpg",
    roughnessUrl: "/assets/textures/polyhaven/cliffRock/roughness.jpg"
  },
  {
    id: "mossRock",
    name: "Moss Rock",
    albedoUrl: "/assets/textures/polyhaven/mossRock/albedo.jpg",
    normalUrl: "/assets/textures/polyhaven/mossRock/normal.jpg",
    roughnessUrl: "/assets/textures/polyhaven/mossRock/roughness.jpg"
  },
  {
    id: "redSoil",
    name: "Red Soil",
    albedoUrl: "/assets/textures/polyhaven/redSoil/albedo.jpg",
    normalUrl: "/assets/textures/polyhaven/redSoil/normal.jpg",
    roughnessUrl: "/assets/textures/polyhaven/redSoil/roughness.jpg"
  },
  {
    id: "snow",
    name: "Snow",
    albedoUrl: "/assets/textures/polyhaven/snow/albedo.jpg",
    normalUrl: "/assets/textures/polyhaven/snow/normal.jpg",
    roughnessUrl: "/assets/textures/polyhaven/snow/roughness.jpg"
  }
] as const;

export type TerrainMaterialId = typeof TERRAIN_MATERIALS[number]["id"];

export type TerrainMaterialWeight = {
  readonly material: TerrainMaterialId;
  readonly weight: number;
};

export type PackedTerrainMaterialWeights = {
  readonly indices: readonly [number, number, number, number];
  readonly weights: readonly [number, number, number, number];
};

export const TERRAIN_MATERIAL_LAYER_COUNT = TERRAIN_MATERIALS.length;
export const DEFAULT_TERRAIN_MATERIAL_ID: TerrainMaterialId = "meadowGrass";
export const DEFAULT_TERRAIN_MATERIAL_PACK: PackedTerrainMaterialWeights = Object.freeze({
  indices: Object.freeze([0, 0, 0, 0] as const),
  weights: Object.freeze([1, 0, 0, 0] as const)
});

const TERRAIN_MATERIAL_LAYER_BY_ID = new Map<TerrainMaterialId, number>(
  TERRAIN_MATERIALS.map((material, index) => [material.id, index])
);

export function terrainMaterialLayer(material: TerrainMaterialId): number {
  const layer = TERRAIN_MATERIAL_LAYER_BY_ID.get(material);
  if (layer === undefined) {
    throw new Error(`Unknown terrain material '${String(material)}'.`);
  }

  return layer;
}

export function normalizeTerrainMaterialWeights(
  weights: readonly TerrainMaterialWeight[]
): readonly TerrainMaterialWeight[] {
  const positiveWeights = weights.filter((weight) => weight.weight > 0);
  const total = positiveWeights.reduce((sum, weight) => sum + weight.weight, 0);
  if (total <= Number.EPSILON) {
    return Object.freeze([
      Object.freeze({ material: DEFAULT_TERRAIN_MATERIAL_ID, weight: 1 })
    ]);
  }

  return Object.freeze(positiveWeights.map((weight) =>
    Object.freeze({
      material: weight.material,
      weight: weight.weight / total
    })
  ));
}

export function packTerrainMaterialWeights(
  weights: readonly TerrainMaterialWeight[]
): PackedTerrainMaterialWeights {
  const normalized = normalizeTerrainMaterialWeights(weights)
    .slice()
    .sort((a, b) => b.weight - a.weight)
    .slice(0, 4);
  const total = normalized.reduce((sum, weight) => sum + weight.weight, 0);
  const indices = [0, 0, 0, 0] as [number, number, number, number];
  const packedWeights = [0, 0, 0, 0] as [number, number, number, number];

  for (let index = 0; index < normalized.length; index += 1) {
    indices[index] = terrainMaterialLayer(normalized[index].material);
    packedWeights[index] = normalized[index].weight / total;
  }

  if (packedWeights[0] === 0) {
    return DEFAULT_TERRAIN_MATERIAL_PACK;
  }

  return Object.freeze({
    indices: Object.freeze(indices),
    weights: Object.freeze(packedWeights)
  });
}
