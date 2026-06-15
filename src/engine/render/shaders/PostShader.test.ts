import { equal, ok } from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import {
  POST_SHADER_METADATA,
  POST_SHADER_SOURCE
} from "../../../generated/render/postShader.js";

describe("post shader build", () => {
  it("exposes the generated WGSL shader artifact", () => {
    equal(POST_SHADER_METADATA.id, "post");
    equal(POST_SHADER_METADATA.language, "wgsl");
    equal(POST_SHADER_METADATA.sourcePath, "src/engine/render/shaders/post.wgsl");
    equal(POST_SHADER_METADATA.vertexEntryPoint, "postVertexMain");
    equal(POST_SHADER_METADATA.fragmentEntryPoint, "postFragmentMain");
    ok(POST_SHADER_SOURCE.includes("@vertex"));
    ok(POST_SHADER_SOURCE.includes("@fragment"));
  });

  it("records a deterministic source hash", () => {
    ok(/^sha256-[0-9a-f]{64}$/.test(POST_SHADER_METADATA.sourceHash));
  });

  it("recomputes the generated source hash from the WGSL source", () => {
    const source = readFileSync(POST_SHADER_METADATA.sourcePath, "utf8")
      .replace(/\r\n/g, "\n")
      .replace(/\r/g, "\n");
    const sourceHash = `sha256-${createHash("sha256").update(source, "utf8").digest("hex")}`;

    equal(POST_SHADER_METADATA.sourceHash, sourceHash);
    equal(POST_SHADER_SOURCE, source);
  });

  it("contains the post-process resource and debug-view contract", () => {
    ok(POST_SHADER_SOURCE.includes("sceneColorTexture: texture_2d<f32>"));
    ok(POST_SHADER_SOURCE.includes("linearDepthTexture: texture_2d<f32>"));
    ok(POST_SHADER_SOURCE.includes("POST_DEBUG_FINAL"));
    ok(POST_SHADER_SOURCE.includes("POST_DEBUG_SCENE_COLOR"));
    ok(POST_SHADER_SOURCE.includes("POST_DEBUG_LINEAR_DEPTH"));
    ok(POST_SHADER_SOURCE.includes("POST_DEBUG_POST_TONE_MAP"));
    ok(POST_SHADER_SOURCE.includes("POST_DEBUG_BLOOM"));
    ok(POST_SHADER_SOURCE.includes("POST_DEBUG_DOF_COC"));
    ok(POST_SHADER_SOURCE.includes("POST_DEBUG_DOF_BLURRED"));
    ok(POST_SHADER_SOURCE.includes("POST_DEBUG_FOG_FACTOR"));
    ok(POST_SHADER_SOURCE.includes("textureLoad(linearDepthTexture"));
  });

  it("owns filmic tone mapping for final presentation", () => {
    ok(POST_SHADER_SOURCE.includes("fn acesFilmic"));
    ok(POST_SHADER_SOURCE.includes("fn applyToneMap"));
    ok(POST_SHADER_SOURCE.includes("debugViewAndScale"));
  });

  it("contains the bloom extraction and composite contract", () => {
    ok(POST_SHADER_SOURCE.includes("fn bloomFragmentMain"));
    ok(POST_SHADER_SOURCE.includes("fn bloomBrightColor"));
    ok(POST_SHADER_SOURCE.includes("bloomSettings"));
    ok(POST_SHADER_SOURCE.includes("bloomTexture: texture_2d<f32>"));
  });

  it("contains the depth-of-field CoC and blur contract", () => {
    ok(POST_SHADER_SOURCE.includes("fn dofCocPixels"));
    ok(POST_SHADER_SOURCE.includes("fn dofBlurredSceneColor"));
    ok(POST_SHADER_SOURCE.includes("dofSettings"));
  });

  it("contains the depth-based horizon fog contract", () => {
    ok(POST_SHADER_SOURCE.includes("fogSettings"));
    ok(POST_SHADER_SOURCE.includes("fogColorAndCurve"));
    ok(POST_SHADER_SOURCE.includes("@group(2) @binding(0) var<uniform> camera: Camera"));
    ok(POST_SHADER_SOURCE.includes("fn fogFactor"));
    ok(POST_SHADER_SOURCE.includes("fn skyColorAtUv"));
    ok(POST_SHADER_SOURCE.includes("fn applyFog"));
    ok(POST_SHADER_SOURCE.includes("linearDepth <= 0.0"));
    ok(POST_SHADER_SOURCE.includes("skyColorAtUv(uv)"));
    ok(POST_SHADER_SOURCE.includes("let foggedColor = applyFog"));
  });

  it("only computes the expensive DoF blur when enabled or explicitly debugged", () => {
    ok(POST_SHADER_SOURCE.includes("let dofBlurredView"));
    ok(POST_SHADER_SOURCE.includes("if (postProcess.dofSettings.x >= 0.5 || dofBlurredView)"));
  });
});
