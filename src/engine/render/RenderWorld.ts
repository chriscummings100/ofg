import type { CameraFrame } from "../camera/cameraRig.js";
import type { Mat4 } from "../math/mat4.js";
import type { Material } from "./Material.js";
import type { Mesh } from "./Mesh.js";

export type RenderWorld = {
  readonly camera: CameraFrame;
  readonly items: readonly RenderItem[];
};

export type RenderItem = {
  readonly mesh: Mesh;
  readonly material?: Material;
  readonly worldMatrix: Mat4;
};
