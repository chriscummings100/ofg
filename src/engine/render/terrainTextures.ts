import { loadRgbaTextureFromUrl } from "./textureLoader.js";

export const TERRAIN_ALBEDO_TEXTURE_ID = "texture:terrain.albedo";
export const TERRAIN_ALBEDO_ATLAS_URL = "/assets/textures/terrain-albedo-atlas.png";
export const TERRAIN_ALBEDO_ATLAS_TILE_COUNT = 3;
export const TERRAIN_ALBEDO_ATLAS_TILE_SIZE = 724;
export const TERRAIN_ALBEDO_ATLAS_WIDTH =
  TERRAIN_ALBEDO_ATLAS_TILE_SIZE * TERRAIN_ALBEDO_ATLAS_TILE_COUNT;
export const TERRAIN_ALBEDO_ATLAS_HEIGHT = TERRAIN_ALBEDO_ATLAS_TILE_SIZE;

export async function loadTerrainAlbedoTexture() {
  return loadRgbaTextureFromUrl(TERRAIN_ALBEDO_TEXTURE_ID, TERRAIN_ALBEDO_ATLAS_URL);
}
