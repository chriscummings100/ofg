import type { ResourceId } from "../scene/types.js";

export type TextureFormat = "rgba8unorm";

export type TextureOptions = {
  readonly data?: Uint8Array | Uint8ClampedArray;
};

export class Texture {
  readonly id: ResourceId;
  readonly width: number;
  readonly height: number;
  readonly format: TextureFormat;
  readonly data?: Uint8Array | Uint8ClampedArray;

  constructor(
    id: ResourceId,
    width: number,
    height: number,
    format: TextureFormat,
    options: TextureOptions = {}
  ) {
    if (width <= 0 || height <= 0) {
      throw new Error("Texture dimensions must be positive.");
    }

    const data = options.data;
    if (data !== undefined && data.length !== width * height * 4) {
      throw new Error("Texture rgba8unorm data must contain width * height * 4 bytes.");
    }

    this.id = id;
    this.width = width;
    this.height = height;
    this.format = format;
    this.data = data;
  }
}
