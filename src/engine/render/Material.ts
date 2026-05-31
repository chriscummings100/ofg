import { vec3, type Vec3 } from "../math/vec3.js";
import { vec4, type Vec4 } from "../math/vec4.js";
import type { ResourceId } from "../scene/types.js";

export type MaterialOptions = {
  readonly albedoFactor?: Vec4;
  readonly albedoTexture?: ResourceId;
  readonly specular?: Vec3;
  readonly specularFactor?: number;
  readonly flags?: number;
};

export const DEFAULT_ALBEDO_FACTOR = Object.freeze(vec4(1, 1, 1, 1));
export const DEFAULT_SPECULAR = Object.freeze(vec3(1, 1, 1));
export const DEFAULT_SPECULAR_FACTOR = 0.18;

export class Material {
  readonly id: ResourceId;
  albedoFactor: Vec4;
  albedoTexture?: ResourceId;
  specular: Vec3;
  specularFactor: number;
  flags: number;

  constructor(id: ResourceId, options: MaterialOptions = {}) {
    this.id = id;
    this.albedoFactor = options.albedoFactor ?? DEFAULT_ALBEDO_FACTOR;
    this.albedoTexture = options.albedoTexture;
    this.specular = options.specular ?? DEFAULT_SPECULAR;
    this.specularFactor = options.specularFactor ?? DEFAULT_SPECULAR_FACTOR;
    this.flags = options.flags ?? 0;
  }
}
