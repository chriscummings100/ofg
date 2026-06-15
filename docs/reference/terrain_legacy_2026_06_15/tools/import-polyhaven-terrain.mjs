import { mkdirSync, writeFileSync, existsSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const outputRoot = resolve(root, "assets", "textures", "polyhaven");
const userAgent = "ofg-dev-texture-import/0.1 (local development)";
const resolution = "1k";

const materials = [
  { id: "meadowGrass", name: "Meadow Grass", slug: "grass_path_3" },
  { id: "dryGround", name: "Dry Ground", slug: "dry_ground_01" },
  { id: "forestGround", name: "Forest Ground", slug: "forest_ground_04" },
  { id: "leafLitter", name: "Leaf Litter", slug: "dry_decay_leaves" },
  { id: "bareSoil", name: "Bare Soil", slug: "aerial_ground_rock" },
  { id: "dryMud", name: "Dry Mud", slug: "brown_mud_dry" },
  { id: "wetMud", name: "Wet Mud", slug: "brown_mud" },
  { id: "sand", name: "Sand", slug: "coast_sand_01" },
  { id: "gravelSand", name: "Gravel Sand", slug: "gravelly_sand" },
  { id: "riverPebbles", name: "River Pebbles", slug: "ganges_river_pebbles" },
  { id: "scree", name: "Scree", slug: "rocks_ground_02" },
  { id: "rockyGround", name: "Rocky Ground", slug: "rocky_terrain_02" },
  { id: "cliffRock", name: "Cliff Rock", slug: "rock_face_03" },
  { id: "mossRock", name: "Moss Rock", slug: "lichen_rock" },
  { id: "redSoil", name: "Red Soil", slug: "red_laterite_soil_stones" },
  { id: "snow", name: "Snow", slug: "snow_02" }
];

const mapKinds = [
  { id: "albedo", polyHavenType: "Diffuse", extension: "jpg" },
  { id: "normal", polyHavenType: "nor_gl", extension: "jpg" },
  { id: "roughness", polyHavenType: "Rough", extension: "jpg" }
];

mkdirSync(outputRoot, { recursive: true });

const manifest = {
  source: "Poly Haven",
  license: "CC0",
  resolution,
  generatedAt: new Date().toISOString(),
  materials: []
};

for (const material of materials) {
  const files = await fetchJson(`https://api.polyhaven.com/files/${material.slug}`);
  const outputMaterial = {
    ...material,
    sourceUrl: `https://polyhaven.com/a/${material.slug}`,
    maps: {}
  };

  for (const mapKind of mapKinds) {
    const file = files[mapKind.polyHavenType]?.[resolution]?.[mapKind.extension];
    if (file === undefined) {
      throw new Error(
        `Poly Haven asset '${material.slug}' is missing ${mapKind.polyHavenType} ` +
        `${resolution} ${mapKind.extension}.`
      );
    }

    const relativePath = `assets/textures/polyhaven/${material.id}/${mapKind.id}.jpg`;
    const outputPath = resolve(root, relativePath);
    mkdirSync(dirname(outputPath), { recursive: true });
    await downloadFile(file.url, outputPath);
    outputMaterial.maps[mapKind.id] = {
      path: relativePath,
      sourceUrl: file.url,
      md5: file.md5,
      size: file.size
    };
  }

  manifest.materials.push(outputMaterial);
}

writeFileSync(
  resolve(outputRoot, "manifest.json"),
  `${JSON.stringify(manifest, null, 2)}\n`
);

console.log(`Imported ${materials.length} Poly Haven terrain materials into ${outputRoot}`);

async function fetchJson(url) {
  const response = await fetch(url, { headers: { "user-agent": userAgent } });
  if (!response.ok) {
    throw new Error(`Failed to fetch ${url}: ${response.status} ${response.statusText}`);
  }

  return response.json();
}

async function downloadFile(url, outputPath) {
  if (existsSync(outputPath)) {
    return;
  }

  const response = await fetch(url, { headers: { "user-agent": userAgent } });
  if (!response.ok) {
    throw new Error(`Failed to download ${url}: ${response.status} ${response.statusText}`);
  }

  const bytes = new Uint8Array(await response.arrayBuffer());
  writeFileSync(outputPath, bytes);
  console.log(`Downloaded ${outputPath}`);
}
