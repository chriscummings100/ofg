import {
  DEFAULT_TERRAIN_MATERIAL_PACK,
  type PackedTerrainMaterialWeights
} from "./terrainMaterials.js";

export type MeshData = {
  readonly vertices: Float32Array;
  readonly indices: Uint32Array;
};

const FLOATS_PER_VERTEX = 19;
export const MATERIAL_INDICES_VERTEX_OFFSET = 11;
export const MATERIAL_WEIGHTS_VERTEX_OFFSET = 15;
export const POSITION_COLOR_NORMAL_UV_LAYOUT = {
  floatsPerVertex: FLOATS_PER_VERTEX,
  attributes: [
    { name: "position", offset: 0, size: 3 },
    { name: "color", offset: 3, size: 3 },
    { name: "normal", offset: 6, size: 3 },
    { name: "uv", offset: 9, size: 2 },
    { name: "materialIndices", offset: MATERIAL_INDICES_VERTEX_OFFSET, size: 4 },
    { name: "materialWeights", offset: MATERIAL_WEIGHTS_VERTEX_OFFSET, size: 4 }
  ]
} as const;

export function getFloatsPerVertex(): number {
  return FLOATS_PER_VERTEX;
}

export function writePackedTerrainMaterial(
  vertices: Float32Array | number[],
  vertexOffset: number,
  material: PackedTerrainMaterialWeights = DEFAULT_TERRAIN_MATERIAL_PACK
): void {
  for (let index = 0; index < 4; index += 1) {
    vertices[vertexOffset + MATERIAL_INDICES_VERTEX_OFFSET + index] = material.indices[index];
    vertices[vertexOffset + MATERIAL_WEIGHTS_VERTEX_OFFSET + index] = material.weights[index];
  }
}

export function expandTerrainMeshForTriangleMaterialPalettes(mesh: MeshData): MeshData {
  if (mesh.vertices.length % FLOATS_PER_VERTEX !== 0) {
    throw new Error("Terrain mesh vertices must use the terrain vertex layout.");
  }

  if (mesh.indices.length % 3 !== 0) {
    throw new Error("Terrain mesh indices must describe complete triangles.");
  }

  const vertexCount = mesh.vertices.length / FLOATS_PER_VERTEX;
  const vertices = new Float32Array(mesh.indices.length * FLOATS_PER_VERTEX);
  const indices = new Uint32Array(mesh.indices.length);

  for (let triangleOffset = 0; triangleOffset < mesh.indices.length; triangleOffset += 3) {
    const sourceVertexIndices = [
      mesh.indices[triangleOffset],
      mesh.indices[triangleOffset + 1],
      mesh.indices[triangleOffset + 2]
    ] as const;
    for (const sourceVertexIndex of sourceVertexIndices) {
      if (sourceVertexIndex >= vertexCount) {
        throw new Error("Terrain mesh indices must reference existing vertices.");
      }
    }

    const palette = triangleMaterialPalette(mesh.vertices, sourceVertexIndices);
    for (let corner = 0; corner < 3; corner += 1) {
      const sourceVertexOffset = sourceVertexIndices[corner] * FLOATS_PER_VERTEX;
      const expandedVertexIndex = triangleOffset + corner;
      const expandedVertexOffset = expandedVertexIndex * FLOATS_PER_VERTEX;

      vertices.set(
        mesh.vertices.subarray(sourceVertexOffset, sourceVertexOffset + FLOATS_PER_VERTEX),
        expandedVertexOffset
      );
      writePackedTerrainMaterial(vertices, expandedVertexOffset, {
        indices: palette,
        weights: vertexWeightsForPalette(mesh.vertices, sourceVertexOffset, palette)
      });
      indices[expandedVertexIndex] = expandedVertexIndex;
    }
  }

  return { vertices, indices };
}

export function colorForHeight(height: number): readonly [number, number, number] {
  if (height > 2.2) {
    return [0.72, 0.75, 0.7];
  }

  if (height > 0.4) {
    return [0.38, 0.48, 0.31];
  }

  if (height < -2.0) {
    return [0.26, 0.35, 0.44];
  }

  return [0.31, 0.55, 0.38];
}

function triangleMaterialPalette(
  vertices: Float32Array,
  sourceVertexIndices: readonly [number, number, number]
): readonly [number, number, number, number] {
  const weightByLayer = new Map<number, number>();

  for (const sourceVertexIndex of sourceVertexIndices) {
    const sourceVertexOffset = sourceVertexIndex * FLOATS_PER_VERTEX;
    for (let slot = 0; slot < 4; slot += 1) {
      const layer = Math.round(vertices[sourceVertexOffset + MATERIAL_INDICES_VERTEX_OFFSET + slot]);
      const weight = vertices[sourceVertexOffset + MATERIAL_WEIGHTS_VERTEX_OFFSET + slot];
      if (weight > 0) {
        weightByLayer.set(layer, (weightByLayer.get(layer) ?? 0) + weight);
      }
    }
  }

  const rankedLayers = [...weightByLayer.entries()]
    .sort((a, b) => b[1] - a[1] || a[0] - b[0])
    .slice(0, 4)
    .map(([layer]) => layer);
  const palette = [0, 0, 0, 0] as [number, number, number, number];
  for (let index = 0; index < rankedLayers.length; index += 1) {
    palette[index] = rankedLayers[index];
  }

  return palette;
}

function vertexWeightsForPalette(
  vertices: Float32Array,
  sourceVertexOffset: number,
  palette: readonly [number, number, number, number]
): readonly [number, number, number, number] {
  const weights = [0, 0, 0, 0] as [number, number, number, number];

  for (let slot = 0; slot < 4; slot += 1) {
    const sourceLayer = Math.round(vertices[sourceVertexOffset + MATERIAL_INDICES_VERTEX_OFFSET + slot]);
    const sourceWeight = vertices[sourceVertexOffset + MATERIAL_WEIGHTS_VERTEX_OFFSET + slot];
    const paletteSlot = palette.indexOf(sourceLayer);
    if (paletteSlot >= 0) {
      weights[paletteSlot] += sourceWeight;
    }
  }

  const total = weights.reduce((sum, weight) => sum + weight, 0);
  if (total <= Number.EPSILON) {
    weights[0] = 1;
    return weights;
  }

  for (let index = 0; index < weights.length; index += 1) {
    weights[index] /= total;
  }

  return weights;
}
