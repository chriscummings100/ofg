// Verifies the generated water shader artifact and depth-model contracts.

import { equal, ok } from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import {
  WATER_SHADER_METADATA,
  WATER_SHADER_SOURCE
} from "../../../generated/render/waterShader.js";

describe("water shader build", () => {
  it("exposes the generated WGSL shader artifact", () => {
    equal(WATER_SHADER_METADATA.id, "water");
    equal(WATER_SHADER_METADATA.language, "wgsl");
    equal(WATER_SHADER_METADATA.sourcePath, "src/engine/render/shaders/water.wgsl");
    equal(WATER_SHADER_METADATA.vertexEntryPoint, "waterCopyVertexMain");
    equal(WATER_SHADER_METADATA.fragmentEntryPoint, "waterCopyFragmentMain");
    equal(WATER_SHADER_METADATA.waterPatchVertexEntryPoint, "waterPatchVertexMain");
    equal(WATER_SHADER_METADATA.waterPatchFragmentEntryPoint, "waterPatchFragmentMain");
    ok(WATER_SHADER_SOURCE.includes("@vertex"));
    ok(WATER_SHADER_SOURCE.includes("@fragment"));
  });

  it("records a deterministic source hash", () => {
    ok(/^sha256-[0-9a-f]{64}$/.test(WATER_SHADER_METADATA.sourceHash));
  });

  it("recomputes the generated source hash from the WGSL source", () => {
    const source = readFileSync(WATER_SHADER_METADATA.sourcePath, "utf8")
      .replace(/\r\n/g, "\n")
      .replace(/\r/g, "\n");
    const sourceHash = `sha256-${createHash("sha256").update(source, "utf8").digest("hex")}`;

    equal(WATER_SHADER_METADATA.sourceHash, sourceHash);
    equal(WATER_SHADER_SOURCE, source);
  });

  it("keeps vertical bottom depth separate from view-ray path length", () => {
    ok(WATER_SHADER_SOURCE.includes("bathymetryTexture: texture_2d<f32>"));
    ok(WATER_SHADER_SOURCE.includes("fn loadBathymetryDepth"));
    ok(WATER_SHADER_SOURCE.includes("fn loadBathymetryTexel"));
    ok(WATER_SHADER_SOURCE.includes("let blend = pixel - basePixel"));
    ok(WATER_SHADER_SOURCE.includes("return mix(mix(depth00, depth10, blend.x)"));
    ok(WATER_SHADER_SOURCE.includes("fn bathymetryGradient"));
    ok(WATER_SHADER_SOURCE.includes("let pathLength = select("));
    ok(WATER_SHADER_SOURCE.includes("opaqueDepth - waterDistance"));
    ok(WATER_SHADER_SOURCE.includes("bottomDepth <= 0.03"));
    ok(WATER_SHADER_SOURCE.includes("Terrain workers provide node-local bathymetry tiles."));
  });

  it("contains small ripple and procedural foam contracts", () => {
    ok(WATER_SHADER_SOURCE.includes("fn waveNormal"));
    ok(WATER_SHADER_SOURCE.includes("fn rippleSlope"));
    ok(WATER_SHADER_SOURCE.includes("fn foamAmount"));
    ok(WATER_SHADER_SOURCE.includes("fn fbmNoise"));
    ok(WATER_SHADER_SOURCE.includes("edgeDensityFloor"));
    ok(WATER_SHADER_SOURCE.includes("foamColor"));
  });

  it("contains planar reflection and debug-view contracts", () => {
    ok(WATER_SHADER_SOURCE.includes("reflectionViewProjection"));
    ok(WATER_SHADER_SOURCE.includes("reflectionColorTexture"));
    ok(WATER_SHADER_SOURCE.includes("WATER_DEBUG_BOTTOM_DEPTH"));
    ok(WATER_SHADER_SOURCE.includes("WATER_DEBUG_PATH_LENGTH"));
    ok(WATER_SHADER_SOURCE.includes("WATER_DEBUG_FRESNEL"));
    ok(WATER_SHADER_SOURCE.includes("WATER_DEBUG_REFLECTION"));
  });

  it("writes scene color and linear depth for downstream post-process", () => {
    ok(WATER_SHADER_SOURCE.includes("struct WaterFragmentOutput"));
    ok(WATER_SHADER_SOURCE.includes("@location(0) color: vec4<f32>"));
    ok(WATER_SHADER_SOURCE.includes("@location(1) linearDepth: f32"));
    ok(WATER_SHADER_SOURCE.includes("output.linearDepth = waterDistance"));
  });
});
