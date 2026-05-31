import { inverseMat4, transposeMat4, type Mat4 } from "../math/mat4.js";
import {
  DEFAULT_ALBEDO_FACTOR,
  DEFAULT_SPECULAR,
  DEFAULT_SPECULAR_FACTOR,
  type Material
} from "./Material.js";

export const OBJECT_UNIFORM_FLOATS = 40;
export const OBJECT_UNIFORM_BYTES = OBJECT_UNIFORM_FLOATS * Float32Array.BYTES_PER_ELEMENT;

export function buildObjectUniformValues(
  worldMatrix: Mat4,
  material?: Material,
  target: Float32Array<ArrayBufferLike> = new Float32Array(OBJECT_UNIFORM_FLOATS)
): Float32Array<ArrayBufferLike> {
  const albedo = material?.albedoFactor ?? DEFAULT_ALBEDO_FACTOR;
  const specular = material?.specular ?? DEFAULT_SPECULAR;
  const specularFactor = material?.specularFactor ?? DEFAULT_SPECULAR_FACTOR;
  const normalMatrix = transposeMat4(inverseMat4(worldMatrix));

  target.set(worldMatrix, 0);
  target.set(normalMatrix, 16);
  target[32] = albedo.x;
  target[33] = albedo.y;
  target[34] = albedo.z;
  target[35] = albedo.w;
  target[36] = specular.x;
  target[37] = specular.y;
  target[38] = specular.z;
  target[39] = specularFactor;

  return target;
}
