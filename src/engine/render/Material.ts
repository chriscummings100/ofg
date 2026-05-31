import type { Vec4 } from "../math/vec4.js";
import type { ResourceId } from "../scene/types.js";

export class Material {
  readonly id: ResourceId;
  baseColor: Vec4;
  texture?: ResourceId;
  flags: number;

  constructor(id: ResourceId, baseColor: Vec4, flags = 0, texture?: ResourceId) {
    this.id = id;
    this.baseColor = baseColor;
    this.flags = flags;
    this.texture = texture;
  }
}
