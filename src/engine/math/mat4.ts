import { cross, dot, normalize, subtract, VEC3_UP, type Vec3 } from "./vec3.js";

export type Mat4 = Float32Array;

export function identityMat4(): Mat4 {
  return new Float32Array([
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
  ]);
}

export function perspectiveMat4(
  fovYRadians: number,
  aspect: number,
  near: number,
  far: number
): Mat4 {
  const f = 1 / Math.tan(fovYRadians / 2);
  const rangeInv = 1 / (near - far);

  return new Float32Array([
    f / aspect, 0, 0, 0,
    0, f, 0, 0,
    0, 0, far * rangeInv, -1,
    0, 0, far * near * rangeInv, 0
  ]);
}

export function lookAtMat4(eye: Vec3, target: Vec3, up: Vec3 = VEC3_UP): Mat4 {
  const zAxis = normalize(subtract(eye, target));
  const xAxis = normalize(cross(up, zAxis));
  const yAxis = cross(zAxis, xAxis);

  return new Float32Array([
    xAxis.x, yAxis.x, zAxis.x, 0,
    xAxis.y, yAxis.y, zAxis.y, 0,
    xAxis.z, yAxis.z, zAxis.z, 0,
    -dot(xAxis, eye), -dot(yAxis, eye), -dot(zAxis, eye), 1
  ]);
}

export function multiplyMat4(a: Mat4, b: Mat4): Mat4 {
  const out = new Float32Array(16);

  for (let column = 0; column < 4; column += 1) {
    for (let row = 0; row < 4; row += 1) {
      out[column * 4 + row] =
        a[0 * 4 + row] * b[column * 4 + 0] +
        a[1 * 4 + row] * b[column * 4 + 1] +
        a[2 * 4 + row] * b[column * 4 + 2] +
        a[3 * 4 + row] * b[column * 4 + 3];
    }
  }

  return out;
}

export function transposeMat4(matrix: Mat4): Mat4 {
  return new Float32Array([
    matrix[0], matrix[4], matrix[8], matrix[12],
    matrix[1], matrix[5], matrix[9], matrix[13],
    matrix[2], matrix[6], matrix[10], matrix[14],
    matrix[3], matrix[7], matrix[11], matrix[15]
  ]);
}

export function inverseMat4(matrix: Mat4): Mat4 {
  const a00 = matrix[0];
  const a01 = matrix[1];
  const a02 = matrix[2];
  const a03 = matrix[3];
  const a10 = matrix[4];
  const a11 = matrix[5];
  const a12 = matrix[6];
  const a13 = matrix[7];
  const a20 = matrix[8];
  const a21 = matrix[9];
  const a22 = matrix[10];
  const a23 = matrix[11];
  const a30 = matrix[12];
  const a31 = matrix[13];
  const a32 = matrix[14];
  const a33 = matrix[15];

  const b00 = a00 * a11 - a01 * a10;
  const b01 = a00 * a12 - a02 * a10;
  const b02 = a00 * a13 - a03 * a10;
  const b03 = a01 * a12 - a02 * a11;
  const b04 = a01 * a13 - a03 * a11;
  const b05 = a02 * a13 - a03 * a12;
  const b06 = a20 * a31 - a21 * a30;
  const b07 = a20 * a32 - a22 * a30;
  const b08 = a20 * a33 - a23 * a30;
  const b09 = a21 * a32 - a22 * a31;
  const b10 = a21 * a33 - a23 * a31;
  const b11 = a22 * a33 - a23 * a32;

  let determinant = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 -
    b04 * b07 + b05 * b06;
  if (Math.abs(determinant) <= Number.EPSILON) {
    throw new Error("Matrix cannot be inverted.");
  }

  determinant = 1 / determinant;

  return new Float32Array([
    (a11 * b11 - a12 * b10 + a13 * b09) * determinant,
    (a02 * b10 - a01 * b11 - a03 * b09) * determinant,
    (a31 * b05 - a32 * b04 + a33 * b03) * determinant,
    (a22 * b04 - a21 * b05 - a23 * b03) * determinant,
    (a12 * b08 - a10 * b11 - a13 * b07) * determinant,
    (a00 * b11 - a02 * b08 + a03 * b07) * determinant,
    (a32 * b02 - a30 * b05 - a33 * b01) * determinant,
    (a20 * b05 - a22 * b02 + a23 * b01) * determinant,
    (a10 * b10 - a11 * b08 + a13 * b06) * determinant,
    (a01 * b08 - a00 * b10 - a03 * b06) * determinant,
    (a30 * b04 - a31 * b02 + a33 * b00) * determinant,
    (a21 * b02 - a20 * b04 - a23 * b00) * determinant,
    (a11 * b07 - a10 * b09 - a12 * b06) * determinant,
    (a00 * b09 - a01 * b07 + a02 * b06) * determinant,
    (a31 * b01 - a30 * b03 - a32 * b00) * determinant,
    (a20 * b03 - a21 * b01 + a22 * b00) * determinant
  ]);
}
