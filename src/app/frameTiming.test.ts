import { equal } from "node:assert/strict";
import { computeFrameDeltaSeconds } from "./frameTiming.js";

describe("frameTiming", () => {
  it("computes frame delta seconds from millisecond timestamps", () => {
    equal(computeFrameDeltaSeconds(1250, 1000, 1), 0.25);
  });

  it("caps large frame deltas", () => {
    equal(computeFrameDeltaSeconds(2000, 1000, 0.05), 0.05);
  });

  it("clamps negative first-frame deltas to zero", () => {
    equal(computeFrameDeltaSeconds(999.5, 1000), 0);
  });

  it("clamps non-finite deltas to zero", () => {
    equal(computeFrameDeltaSeconds(Number.NaN, 1000), 0);
  });
});
