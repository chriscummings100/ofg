import type { ResourceId } from "../scene/types.js";

export type TextureFormat = "rgba8unorm";

export class Texture {
  readonly id: ResourceId;
  readonly width: number;
  readonly height: number;
  readonly format: TextureFormat;

  constructor(id: ResourceId, width: number, height: number, format: TextureFormat) {
    this.id = id;
    this.width = width;
    this.height = height;
    this.format = format;
  }
}
