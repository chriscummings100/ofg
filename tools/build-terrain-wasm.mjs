import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const checkOnly = process.argv.includes("--check");
const crateName = "terrain_core";
const packageName = "terrain_core";
const target = "wasm32-unknown-unknown";
const cargoArtifactPath = `target/${target}/release/${crateName}.wasm`;
const assetPath = "assets/wasm/terrain_core.wasm";
const expectedExports = [
  "memory",
  "ofg_terrain_core_version",
  "ofg_terrain_core_preset_count",
  "ofg_terrain_variant_flat_value_count",
  "ofg_terrain_variant_buffer_ptr",
  "ofg_write_terrain_variant_preset",
  "ofg_build_chunk_mesh",
  "ofg_build_chunk_mesh_for_variant",
  "ofg_mesh_vertex_buffer_ptr",
  "ofg_mesh_vertex_buffer_len",
  "ofg_mesh_index_buffer_ptr",
  "ofg_mesh_index_buffer_len",
  "ofg_height_at"
];

const build = spawnSync(
  "cargo",
  ["build", "-p", packageName, "--target", target, "--release"],
  {
    cwd: root,
    stdio: "inherit"
  }
);

if (build.status !== 0) {
  process.exitCode = build.status ?? 1;
  process.exit();
}

const wasmBytes = readFileSync(resolve(root, cargoArtifactPath));
const assetAbsolutePath = resolve(root, assetPath);
const currentAsset = existsSync(assetAbsolutePath)
  ? readFileSync(assetAbsolutePath)
  : undefined;

let hasMismatch = false;
const wasmExports = new Set(
  WebAssembly.Module.exports(new WebAssembly.Module(wasmBytes)).map((entry) => entry.name)
);
const missingExports = expectedExports.filter((name) => !wasmExports.has(name));
if (missingExports.length > 0) {
  console.error(`Terrain WASM artifact is missing exports: ${missingExports.join(", ")}`);
  hasMismatch = true;
}

if (!currentAsset || !Buffer.from(currentAsset).equals(Buffer.from(wasmBytes))) {
  if (checkOnly) {
    console.error(`Terrain WASM asset is stale: ${assetPath}`);
    hasMismatch = true;
  } else {
    mkdirSync(dirname(assetAbsolutePath), { recursive: true });
    writeFileSync(assetAbsolutePath, wasmBytes);
    console.log(`Generated ${assetPath}`);
  }
}

if (hasMismatch) {
  process.exitCode = 1;
}
