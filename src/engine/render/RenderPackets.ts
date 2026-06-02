import type { Mat4 } from "../math/mat4.js";
import type { Material } from "./Material.js";
import type { ResourceId } from "./ResourceId.js";
import type { Texture } from "./Texture.js";

export type RenderMeshPacket = {
  readonly id: ResourceId;
  readonly vertices: Float32Array;
  readonly indices: Uint32Array;
  readonly floatsPerVertex: number;
};

export type RenderItemPacket = {
  readonly id: ResourceId;
  readonly mesh: RenderMeshPacket;
  readonly material?: Material;
  readonly albedoTexture?: Texture;
  readonly normalTexture?: Texture;
  readonly materialTexture?: Texture;
  readonly worldMatrix?: Mat4;
};
