import { ok, equal } from "node:assert/strict";
import { quat, quatFromYaw, normalizeQuat, QUAT_IDENTITY, rotateVec3ByQuat } from "./quat.js";
import { vec3 } from "./vec3.js";

describe("quat", () => {
  it("normalizes quaternions", () => {
    const normalized = normalizeQuat(quat(0, 2, 0, 0));

    equal(normalized.x, 0);
    equal(normalized.y, 1);
    equal(normalized.z, 0);
    equal(normalized.w, 0);
  });

  it("returns identity for zero-length quaternions", () => {
    equal(normalizeQuat(quat(0, 0, 0, 0)), QUAT_IDENTITY);
  });

  it("rotates vectors by yaw", () => {
    const rotated = rotateVec3ByQuat(vec3(0, 0, 1), quatFromYaw(Math.PI / 2));

    ok(Math.abs(rotated.x - 1) < 1e-12);
    ok(Math.abs(rotated.z) < 1e-12);
  });
});
