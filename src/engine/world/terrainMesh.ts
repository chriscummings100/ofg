import type { TerrainField } from "./scalarField.js";

export type MeshData = {
  readonly vertices: Float32Array;
  readonly indices: Uint32Array;
};

export type HeightfieldMeshOptions = {
  readonly halfExtent: number;
  readonly cellsPerAxis: number;
};

const FLOATS_PER_VERTEX = 6;

export function getFloatsPerVertex(): number {
  return FLOATS_PER_VERTEX;
}

export function buildHeightfieldMesh(
  field: TerrainField,
  options: HeightfieldMeshOptions
): MeshData {
  const vertexCountPerAxis = options.cellsPerAxis + 1;
  const step = (options.halfExtent * 2) / options.cellsPerAxis;
  const vertices = new Float32Array(vertexCountPerAxis * vertexCountPerAxis * FLOATS_PER_VERTEX);
  const indices = new Uint32Array(options.cellsPerAxis * options.cellsPerAxis * 6);

  let vertexOffset = 0;
  for (let zIndex = 0; zIndex < vertexCountPerAxis; zIndex += 1) {
    const z = -options.halfExtent + zIndex * step;

    for (let xIndex = 0; xIndex < vertexCountPerAxis; xIndex += 1) {
      const x = -options.halfExtent + xIndex * step;
      const y = field.heightAt(x, z);
      const color = colorForHeight(y);

      vertices[vertexOffset + 0] = x;
      vertices[vertexOffset + 1] = y;
      vertices[vertexOffset + 2] = z;
      vertices[vertexOffset + 3] = color[0];
      vertices[vertexOffset + 4] = color[1];
      vertices[vertexOffset + 5] = color[2];
      vertexOffset += FLOATS_PER_VERTEX;
    }
  }

  let indexOffset = 0;
  for (let zIndex = 0; zIndex < options.cellsPerAxis; zIndex += 1) {
    for (let xIndex = 0; xIndex < options.cellsPerAxis; xIndex += 1) {
      const topLeft = zIndex * vertexCountPerAxis + xIndex;
      const topRight = topLeft + 1;
      const bottomLeft = topLeft + vertexCountPerAxis;
      const bottomRight = bottomLeft + 1;

      indices[indexOffset + 0] = topLeft;
      indices[indexOffset + 1] = bottomLeft;
      indices[indexOffset + 2] = topRight;
      indices[indexOffset + 3] = topRight;
      indices[indexOffset + 4] = bottomLeft;
      indices[indexOffset + 5] = bottomRight;
      indexOffset += 6;
    }
  }

  return { vertices, indices };
}

function colorForHeight(height: number): readonly [number, number, number] {
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
