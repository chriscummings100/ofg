// Guards the browser app entrypoint against importing legacy TypeScript terrain clients.
import { deepEqual, equal, ok } from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { dirname, relative, resolve } from "node:path";

const ROOT = resolve(".");
const SRC_ROOT = resolve(ROOT, "src");
const ENTRYPOINT = resolve(SRC_ROOT, "main.ts");

const FORBIDDEN_RUNTIME_TERRAIN_CLIENTS = [
  "src/engine/world/terrainCoreWasm.ts",
  "src/engine/world/terrainCoreChunkMesh.ts",
  "src/engine/world/terrainCoreDensityChunk.ts",
  "src/engine/world/terrainCoreDensityChunkStore.ts",
  "src/engine/world/terrainCoreStreamScheduler.ts",
  "src/engine/world/terrainMesh.ts",
  "src/generated/terrain/terrainCoreWasm.ts"
] as const;

const ALLOWED_RUNTIME_WORLD_MODULES = [
  "src/engine/world/terrainDescriptor.ts"
] as const;

describe("runtime import graph", () => {
  it("does not keep standalone TypeScript terrain client files in source", () => {
    for (const file of FORBIDDEN_RUNTIME_TERRAIN_CLIENTS) {
      ok(!existsSync(resolve(ROOT, file)), `${file} should stay deleted.`);
    }
  });

  it("keeps the browser runtime from reaching TypeScript terrain clients", () => {
    const reachable = collectRuntimeImports(ENTRYPOINT);
    const reachableTerrainClients = FORBIDDEN_RUNTIME_TERRAIN_CLIENTS.filter((file) =>
      reachable.has(resolve(ROOT, file))
    );

    equal(reachableTerrainClients.join(", "), "");
    ok(reachable.has(resolve(ROOT, "src/engine/world/terrainDescriptor.ts")));
  });

  it("keeps runtime world imports on the browser descriptor allowlist", () => {
    const reachable = collectRuntimeImports(ENTRYPOINT);
    const reachableWorldModules = [...reachable]
      .map((file) => relativeSourcePath(file))
      .filter((file) => file.startsWith("src/engine/world/"))
      .sort();

    deepEqual(reachableWorldModules, [...ALLOWED_RUNTIME_WORLD_MODULES]);
  });

  it("does not reach generated standalone terrain wasm artifacts", () => {
    const reachable = collectRuntimeImports(ENTRYPOINT);
    const reachableGeneratedTerrain = [...reachable]
      .map((file) => relativeSourcePath(file))
      .filter((file) => file.startsWith("src/generated/terrain/"));

    equal(reachableGeneratedTerrain.join(", "), "");
  });
});

function collectRuntimeImports(entrypoint: string): ReadonlySet<string> {
  const visited = new Set<string>();
  const pending = [entrypoint];

  while (pending.length > 0) {
    const file = pending.pop();
    if (file === undefined || visited.has(file)) {
      continue;
    }

    visited.add(file);
    for (const specifier of readRuntimeImportSpecifiers(file)) {
      const resolved = resolveLocalSourceModule(file, specifier);
      if (resolved !== undefined) {
        pending.push(resolved);
      }
    }
  }

  return visited;
}

function readRuntimeImportSpecifiers(file: string): string[] {
  const source = readFileSync(file, "utf8");
  const importPattern = /^\s*import\s+(?!type\b)(?:[\s\S]*?\s+from\s+)?["']([^"']+)["'];?/gm;
  const exportPattern = /^\s*export\s+(?!type\b)(?:[\s\S]*?\s+from\s+)["']([^"']+)["'];?/gm;
  const dynamicImportPattern = /\bimport\s*\(\s*["']([^"']+)["']\s*\)/g;
  const specifiers: string[] = [];

  for (const pattern of [importPattern, exportPattern, dynamicImportPattern]) {
    for (const match of source.matchAll(pattern)) {
      const specifier = match[1];
      if (specifier !== undefined) {
        specifiers.push(specifier);
      }
    }
  }

  return specifiers;
}

function resolveLocalSourceModule(fromFile: string, specifier: string): string | undefined {
  if (!specifier.startsWith(".")) {
    return undefined;
  }

  const base = resolve(dirname(fromFile), specifier);
  const candidates = specifier.endsWith(".js")
    ? [replaceExtension(base, ".js", ".ts"), replaceExtension(base, ".js", ".d.ts"), base]
    : [`${base}.ts`, `${base}.d.ts`, base, resolve(base, "index.ts")];

  return candidates.find((candidate) => existsSync(candidate) && isInsideSourceRoot(candidate));
}

function replaceExtension(path: string, from: string, to: string): string {
  if (!path.endsWith(from)) {
    return path;
  }

  return `${path.slice(0, -from.length)}${to}`;
}

function isInsideSourceRoot(path: string): boolean {
  const sourceRelativePath = relative(SRC_ROOT, resolve(path));
  return sourceRelativePath !== "" && !sourceRelativePath.startsWith("..");
}

function relativeSourcePath(path: string): string {
  return relative(ROOT, resolve(path)).replaceAll("\\", "/");
}
