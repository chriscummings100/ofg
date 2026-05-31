import type { Mat4 } from "../math/mat4.js";
import type { Texture } from "./Texture.js";
import type { CameraFrame } from "./CameraFrame.js";
import type { DirectionalLight } from "./Lighting.js";
import type { Material } from "./Material.js";
import type { Mesh } from "./Mesh.js";

export type RenderWorld = {
  readonly camera: CameraFrame;
  readonly mainLight: DirectionalLight;
  readonly items: readonly RenderItem[];
};

export type RenderItem = {
  readonly id: string;
  readonly mesh: Mesh;
  readonly material?: Material;
  readonly albedoTexture?: Texture;
  readonly worldMatrix: Mat4;
};
