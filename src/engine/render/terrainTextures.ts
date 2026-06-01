import {
  TERRAIN_MATERIAL_LAYER_COUNT,
  TERRAIN_MATERIALS
} from "../world/terrainMaterials.js";
import type { Texture } from "./Texture.js";
import { loadRgbaTextureArrayFromUrls } from "./textureLoader.js";

export const TERRAIN_ALBEDO_TEXTURE_ID = "texture:terrain.albedoArray";
export const TERRAIN_NORMAL_TEXTURE_ID = "texture:terrain.normalArray";
export const TERRAIN_MATERIAL_TEXTURE_ID = "texture:terrain.materialArray";
export const TERRAIN_TEXTURE_ARRAY_LAYER_COUNT = TERRAIN_MATERIAL_LAYER_COUNT;

export type TerrainMaterialTextures = {
  readonly albedo: Texture;
  readonly normal: Texture;
  readonly material: Texture;
};

export async function loadTerrainMaterialTextures(): Promise<TerrainMaterialTextures> {
  const [albedo, normal, material] = await Promise.all([
    loadRgbaTextureArrayFromUrls(
      TERRAIN_ALBEDO_TEXTURE_ID,
      TERRAIN_MATERIALS.map((terrainMaterial) => terrainMaterial.albedoUrl)
    ),
    loadRgbaTextureArrayFromUrls(
      TERRAIN_NORMAL_TEXTURE_ID,
      TERRAIN_MATERIALS.map((terrainMaterial) => terrainMaterial.normalUrl)
    ),
    loadRgbaTextureArrayFromUrls(
      TERRAIN_MATERIAL_TEXTURE_ID,
      TERRAIN_MATERIALS.map((terrainMaterial) => terrainMaterial.roughnessUrl)
    )
  ]);

  return { albedo, normal, material };
}
