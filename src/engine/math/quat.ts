import { vec3, type Vec3 } from "./vec3.js";

export type Quat = {
  readonly x: number;
  readonly y: number;
  readonly z: number;
  readonly w: number;
};

export const QUAT_IDENTITY: Quat = Object.freeze({ x: 0, y: 0, z: 0, w: 1 });

export function quat(x: number, y: number, z: number, w: number): Quat {
  return { x, y, z, w };
}

export function quatFromAxisAngle(axis: Vec3, angleRadians: number): Quat {
  const halfAngle = angleRadians * 0.5;
  const s = Math.sin(halfAngle);

  return normalizeQuat(quat(axis.x * s, axis.y * s, axis.z * s, Math.cos(halfAngle)));
}

export function quatFromYaw(yawRadians: number): Quat {
  return quatFromAxisAngle(vec3(0, 1, 0), yawRadians);
}

export function quatFromYawPitch(yawRadians: number, pitchRadians: number): Quat {
  return multiplyQuat(
    quatFromYaw(yawRadians),
    quatFromAxisAngle(vec3(1, 0, 0), -pitchRadians)
  );
}

export function multiplyQuat(a: Quat, b: Quat): Quat {
  return normalizeQuat(quat(
    a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
    a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
  ));
}

export function normalizeQuat(value: Quat): Quat {
  const length = Math.hypot(value.x, value.y, value.z, value.w);
  if (length <= Number.EPSILON) {
    return QUAT_IDENTITY;
  }

  return quat(value.x / length, value.y / length, value.z / length, value.w / length);
}

export function rotateVec3ByQuat(value: Vec3, rotation: Quat): Vec3 {
  const q = normalizeQuat(rotation);
  const x2 = q.x + q.x;
  const y2 = q.y + q.y;
  const z2 = q.z + q.z;
  const xx = q.x * x2;
  const yy = q.y * y2;
  const zz = q.z * z2;
  const xy = q.x * y2;
  const xz = q.x * z2;
  const yz = q.y * z2;
  const wx = q.w * x2;
  const wy = q.w * y2;
  const wz = q.w * z2;

  return vec3(
    (1 - (yy + zz)) * value.x + (xy - wz) * value.y + (xz + wy) * value.z,
    (xy + wz) * value.x + (1 - (xx + zz)) * value.y + (yz - wx) * value.z,
    (xz - wy) * value.x + (yz + wx) * value.y + (1 - (xx + yy)) * value.z
  );
}
