import type { ResourceId } from "./ResourceId.js";

export type TextureFormat = "rgba8unorm";

export type TextureOptions = {
  readonly data?: Uint8Array | Uint8ClampedArray;
  readonly layers?: number;
};

export class Texture {
  readonly id: ResourceId;
  readonly width: number;
  readonly height: number;
  readonly layers: number;
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

    const layers = options.layers ?? 1;
    if (!Number.isInteger(layers) || layers <= 0) {
      throw new Error("Texture layers must be a positive integer.");
    }

    const data = options.data;
    if (data !== undefined && data.length !== width * height * layers * 4) {
      throw new Error("Texture rgba8unorm data must contain width * height * layers * 4 bytes.");
    }

    this.id = id;
    this.width = width;
    this.height = height;
    this.layers = layers;
    this.format = format;
    this.data = data;
  }
}
