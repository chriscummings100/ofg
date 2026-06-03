import {
  TERRAIN_MATERIAL_LAYER_COUNT,
  TERRAIN_MATERIALS
} from "../world/terrainMaterials.js";
import { loadRgbaTextureArrayFromUrls, type RgbaTextureArray } from "./textureLoader.js";

export const TERRAIN_TEXTURE_ARRAY_LAYER_COUNT = TERRAIN_MATERIAL_LAYER_COUNT;

export type TerrainMaterialTextures = {
  readonly albedo: RgbaTextureArray;
  readonly normal: RgbaTextureArray;
  readonly material: RgbaTextureArray;
};

export async function loadTerrainMaterialTextures(): Promise<TerrainMaterialTextures> {
  const [albedo, normal, material] = await Promise.all([
    loadRgbaTextureArrayFromUrls(
      "terrain albedo array",
      TERRAIN_MATERIALS.map((terrainMaterial) => terrainMaterial.albedoUrl)
    ),
    loadRgbaTextureArrayFromUrls(
      "terrain normal array",
      TERRAIN_MATERIALS.map((terrainMaterial) => terrainMaterial.normalUrl)
    ),
    loadRgbaTextureArrayFromUrls(
      "terrain material array",
      TERRAIN_MATERIALS.map((terrainMaterial) => terrainMaterial.roughnessUrl)
    )
  ]);

  return { albedo, normal, material };
}
