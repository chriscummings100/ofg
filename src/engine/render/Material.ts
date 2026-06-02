import { vec3, type Vec3 } from "../math/vec3.js";
import { vec4, type Vec4 } from "../math/vec4.js";
import type { ResourceId } from "./ResourceId.js";

export type MaterialOptions = {
  readonly albedoFactor?: Vec4;
  readonly albedoTexture?: ResourceId;
  readonly normalTexture?: ResourceId;
  readonly materialTexture?: ResourceId;
  readonly specular?: Vec3;
  readonly specularFactor?: number;
  readonly flags?: number;
  readonly textureScale?: number;
};

export const DEFAULT_ALBEDO_FACTOR = Object.freeze(vec4(1, 1, 1, 1));
export const DEFAULT_SPECULAR = Object.freeze(vec3(1, 1, 1));
export const DEFAULT_SPECULAR_FACTOR = 0.18;
export const DEFAULT_TEXTURE_SCALE = 1;
export const MATERIAL_FLAG_TRIPLANAR_ALBEDO = 1;

export class Material {
  readonly id: ResourceId;
  albedoFactor: Vec4;
  albedoTexture?: ResourceId;
  normalTexture?: ResourceId;
  materialTexture?: ResourceId;
  specular: Vec3;
  specularFactor: number;
  flags: number;
  textureScale: number;

  constructor(id: ResourceId, options: MaterialOptions = {}) {
    const textureScale = options.textureScale ?? DEFAULT_TEXTURE_SCALE;
    if (textureScale <= 0) {
      throw new Error("Material textureScale must be positive.");
    }

    this.id = id;
    this.albedoFactor = options.albedoFactor ?? DEFAULT_ALBEDO_FACTOR;
    this.albedoTexture = options.albedoTexture;
    this.normalTexture = options.normalTexture;
    this.materialTexture = options.materialTexture;
    this.specular = options.specular ?? DEFAULT_SPECULAR;
    this.specularFactor = options.specularFactor ?? DEFAULT_SPECULAR_FACTOR;
    this.flags = options.flags ?? 0;
    this.textureScale = textureScale;
  }
}
