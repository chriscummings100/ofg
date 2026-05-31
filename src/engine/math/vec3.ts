export type Vec3 = {
  readonly x: number;
  readonly y: number;
  readonly z: number;
};

export const VEC3_ZERO: Vec3 = Object.freeze({ x: 0, y: 0, z: 0 });
export const VEC3_UP: Vec3 = Object.freeze({ x: 0, y: 1, z: 0 });

export function vec3(x: number, y: number, z: number): Vec3 {
  return { x, y, z };
}

export function add(a: Vec3, b: Vec3): Vec3 {
  return vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

export function subtract(a: Vec3, b: Vec3): Vec3 {
  return vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

export function scale(v: Vec3, amount: number): Vec3 {
  return vec3(v.x * amount, v.y * amount, v.z * amount);
}

export function dot(a: Vec3, b: Vec3): number {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

export function cross(a: Vec3, b: Vec3): Vec3 {
  return vec3(
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x
  );
}

export function length(v: Vec3): number {
  return Math.hypot(v.x, v.y, v.z);
}

export function normalize(v: Vec3): Vec3 {
  const magnitude = length(v);
  if (magnitude <= Number.EPSILON) {
    return VEC3_ZERO;
  }

  return scale(v, 1 / magnitude);
}

export function distance(a: Vec3, b: Vec3): number {
  return length(subtract(a, b));
}

export function yawPitchForward(yaw: number, pitch: number): Vec3 {
  const cp = Math.cos(pitch);
  return normalize(vec3(Math.sin(yaw) * cp, Math.sin(pitch), Math.cos(yaw) * cp));
}

export function yawRight(yaw: number): Vec3 {
  return normalize(vec3(Math.cos(yaw), 0, -Math.sin(yaw)));
}

export function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}
