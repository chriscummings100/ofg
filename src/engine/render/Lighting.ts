import { normalize, vec3, type Vec3 } from "../math/vec3.js";

export type DirectionalLight = {
  readonly direction: Vec3;
  readonly color: Vec3;
  readonly intensity: number;
  readonly ambient: number;
};

export type DirectionalLightOptions = {
  readonly direction?: Vec3;
  readonly color?: Vec3;
  readonly intensity?: number;
  readonly ambient?: number;
};

export function createDirectionalLight(options: DirectionalLightOptions = {}): DirectionalLight {
  return {
    direction: normalize(options.direction ?? vec3(0.89, 0.25, 0.38)),
    color: options.color ?? vec3(1, 0.96, 0.88),
    intensity: options.intensity ?? 1,
    ambient: options.ambient ?? 0.34
  };
}
