import { ok, throws } from "node:assert/strict";
import { identityMat4, inverseMat4, multiplyMat4, transposeMat4 } from "./mat4.js";

describe("mat4", () => {
  it("inverts identity", () => {
    const inverse = inverseMat4(identityMat4());

    ok(matricesNearlyEqual(inverse, identityMat4()));
  });

  it("inverts translation matrices", () => {
    const matrix = identityMat4();
    matrix[12] = 3;
    matrix[13] = 4;
    matrix[14] = 5;

    const inverse = inverseMat4(matrix);
    const product = multiplyMat4(matrix, inverse);

    ok(matricesNearlyEqual(product, identityMat4()));
  });

  it("throws for singular matrices", () => {
    throws(() => inverseMat4(new Float32Array(16)), /cannot be inverted/);
  });

  it("transposes column-major matrices", () => {
    const matrix = new Float32Array([
      1, 2, 3, 4,
      5, 6, 7, 8,
      9, 10, 11, 12,
      13, 14, 15, 16
    ]);

    const transposed = transposeMat4(matrix);

    ok(matricesNearlyEqual(transposed, new Float32Array([
      1, 5, 9, 13,
      2, 6, 10, 14,
      3, 7, 11, 15,
      4, 8, 12, 16
    ])));
  });
});

function matricesNearlyEqual(a: Float32Array, b: Float32Array): boolean {
  return a.every((value, index) => Math.abs(value - b[index]) < 1e-6);
}
