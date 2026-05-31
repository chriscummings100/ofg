import type { Mat4 } from "../math/mat4.js";
import {
  DEFAULT_ALBEDO_FACTOR,
  DEFAULT_SPECULAR,
  DEFAULT_SPECULAR_FACTOR,
  type Material
} from "./Material.js";

export const OBJECT_UNIFORM_FLOATS = 24;
export const OBJECT_UNIFORM_BYTES = OBJECT_UNIFORM_FLOATS * Float32Array.BYTES_PER_ELEMENT;

export function buildObjectUniformValues(worldMatrix: Mat4, material?: Material): Float32Array {
  const values = new Float32Array(OBJECT_UNIFORM_FLOATS);
  const albedo = material?.albedoFactor ?? DEFAULT_ALBEDO_FACTOR;
  const specular = material?.specular ?? DEFAULT_SPECULAR;
  const specularFactor = material?.specularFactor ?? DEFAULT_SPECULAR_FACTOR;

  values.set(worldMatrix, 0);
  values[16] = albedo.x;
  values[17] = albedo.y;
  values[18] = albedo.z;
  values[19] = albedo.w;
  values[20] = specular.x;
  values[21] = specular.y;
  values[22] = specular.z;
  values[23] = specularFactor;

  return values;
}
