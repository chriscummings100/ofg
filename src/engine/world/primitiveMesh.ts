import { getFloatsPerVertex, type MeshData } from "./terrainMesh.js";
import { normalize, vec3, type Vec3 } from "../math/vec3.js";

export function createBoxMesh(center: Vec3, halfSize: Vec3, color: Vec3): MeshData {
  const corners = [
    [-1, -1, -1],
    [1, -1, -1],
    [1, 1, -1],
    [-1, 1, -1],
    [-1, -1, 1],
    [1, -1, 1],
    [1, 1, 1],
    [-1, 1, 1]
  ] as const;

  const vertices = new Float32Array(corners.length * getFloatsPerVertex());
  for (let index = 0; index < corners.length; index += 1) {
    const [x, y, z] = corners[index];
    const offset = index * getFloatsPerVertex();
    const normal = normalize(vec3(x, y, z));

    vertices[offset + 0] = center.x + x * halfSize.x;
    vertices[offset + 1] = center.y + y * halfSize.y;
    vertices[offset + 2] = center.z + z * halfSize.z;
    vertices[offset + 3] = color.x;
    vertices[offset + 4] = color.y;
    vertices[offset + 5] = color.z;
    vertices[offset + 6] = normal.x;
    vertices[offset + 7] = normal.y;
    vertices[offset + 8] = normal.z;
  }

  const indices = new Uint32Array([
    0, 1, 2, 0, 2, 3,
    4, 6, 5, 4, 7, 6,
    0, 4, 5, 0, 5, 1,
    1, 5, 6, 1, 6, 2,
    2, 6, 7, 2, 7, 3,
    3, 7, 4, 3, 4, 0
  ]);

  return { vertices, indices };
}
