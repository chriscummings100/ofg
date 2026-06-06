import { loadRgbaTextureArrayFromUrls, type RgbaTextureArray } from "./textureLoader.js";

const TERRAIN_TEXTURE_MANIFEST_URL = "/assets/textures/polyhaven/manifest.json";

export const TERRAIN_TEXTURE_ARRAY_LAYER_COUNT = 16;

export type TerrainMaterialTextures = {
  readonly albedo: RgbaTextureArray;
  readonly normal: RgbaTextureArray;
  readonly material: RgbaTextureArray;
};

export type TerrainTextureManifest = {
  readonly source: string;
  readonly license: string;
  readonly materials: readonly TerrainTextureManifestMaterial[];
};

export type TerrainTextureManifestMaterial = {
  readonly id: string;
  readonly name: string;
  readonly slug: string;
  readonly maps: {
    readonly albedo: TerrainTextureManifestMap;
    readonly normal: TerrainTextureManifestMap;
    readonly roughness: TerrainTextureManifestMap;
  };
};

export type TerrainTextureManifestMap = {
  readonly path: string;
};

export type TerrainTextureUrls = {
  readonly albedo: readonly string[];
  readonly normal: readonly string[];
  readonly material: readonly string[];
};

export async function loadTerrainMaterialTextures(): Promise<TerrainMaterialTextures> {
  return loadTerrainMaterialTexturesFromManifest(
    await loadTerrainTextureManifest(TERRAIN_TEXTURE_MANIFEST_URL)
  );
}

export async function loadTerrainMaterialTexturesFromManifest(
  manifest: TerrainTextureManifest
): Promise<TerrainMaterialTextures> {
  const urls = terrainTextureUrlsFromManifest(manifest);
  const [albedo, normal, material] = await Promise.all([
    loadRgbaTextureArrayFromUrls("terrain albedo array", urls.albedo),
    loadRgbaTextureArrayFromUrls("terrain normal array", urls.normal),
    loadRgbaTextureArrayFromUrls("terrain material array", urls.material)
  ]);

  return { albedo, normal, material };
}

export function terrainTextureUrlsFromManifest(
  manifest: TerrainTextureManifest
): TerrainTextureUrls {
  if (manifest.materials.length !== TERRAIN_TEXTURE_ARRAY_LAYER_COUNT) {
    throw new Error(
      `Terrain texture manifest has ${manifest.materials.length} materials; ` +
      `expected ${TERRAIN_TEXTURE_ARRAY_LAYER_COUNT}.`
    );
  }

  return {
    albedo: manifest.materials.map((material) => textureAssetUrl(material.maps.albedo.path)),
    normal: manifest.materials.map((material) => textureAssetUrl(material.maps.normal.path)),
    material: manifest.materials.map((material) => textureAssetUrl(material.maps.roughness.path))
  };
}

async function loadTerrainTextureManifest(url: string): Promise<TerrainTextureManifest> {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(
      `Failed to load terrain texture manifest '${url}': ` +
      `${response.status} ${response.statusText}`
    );
  }

  return await response.json() as TerrainTextureManifest;
}

function textureAssetUrl(path: string): string {
  return path.startsWith("/") ? path : `/${path}`;
}
