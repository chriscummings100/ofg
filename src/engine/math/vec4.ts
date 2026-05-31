export type Vec4 = {
  readonly x: number;
  readonly y: number;
  readonly z: number;
  readonly w: number;
};

export function vec4(x: number, y: number, z: number, w: number): Vec4 {
  return { x, y, z, w };
}
