import { ok } from "node:assert/strict";
import { distance, normalize, vec3, yawPitchForward, yawRight } from "./vec3.js";

describe("vec3", () => {
  it("normalizes non-zero vectors", () => {
    const normalized = normalize(vec3(3, 4, 0));

    ok(Math.abs(normalized.x - 0.6) < 1e-12);
    ok(Math.abs(normalized.y - 0.8) < 1e-12);
    ok(Math.abs(normalized.z) < 1e-12);
  });

  it("keeps yaw basis vectors perpendicular", () => {
    const forward = yawPitchForward(Math.PI / 3, 0);
    const right = yawRight(Math.PI / 3);

    ok(distance(forward, vec3(Math.sin(Math.PI / 3), 0, Math.cos(Math.PI / 3))) < 1e-12);
    ok(Math.abs(forward.x * right.x + forward.z * right.z) < 1e-12);
  });
});
