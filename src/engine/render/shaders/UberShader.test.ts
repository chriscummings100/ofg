import { equal, ok } from "node:assert/strict";
import {
  UBER_SHADER_METADATA,
  UBER_SHADER_SOURCE
} from "../../../generated/render/uberShader.js";

describe("uber shader build", () => {
  it("exposes the generated WGSL shader artifact", () => {
    equal(UBER_SHADER_METADATA.id, "uber");
    equal(UBER_SHADER_METADATA.language, "wgsl");
    equal(UBER_SHADER_METADATA.sourcePath, "src/engine/render/shaders/uber.wgsl");
    equal(UBER_SHADER_METADATA.vertexEntryPoint, "vertexMain");
    equal(UBER_SHADER_METADATA.modelVertexEntryPoint, "modelVertexMain");
    equal(UBER_SHADER_METADATA.fragmentEntryPoint, "fragmentMain");
    equal(UBER_SHADER_METADATA.skyVertexEntryPoint, "skyVertexMain");
    equal(UBER_SHADER_METADATA.skyFragmentEntryPoint, "skyFragmentMain");
    ok(UBER_SHADER_SOURCE.includes("@vertex"));
    ok(UBER_SHADER_SOURCE.includes("@fragment"));
  });

  it("records a deterministic source hash", () => {
    ok(/^sha256-[0-9a-f]{64}$/.test(UBER_SHADER_METADATA.sourceHash));
  });

  it("matches the renderer vertex layout contract", () => {
    ok(UBER_SHADER_SOURCE.includes("@location(0) position: vec3<f32>"));
    ok(UBER_SHADER_SOURCE.includes("@location(1) color: vec3<f32>"));
    ok(UBER_SHADER_SOURCE.includes("@location(2) normal: vec3<f32>"));
    ok(UBER_SHADER_SOURCE.includes("@location(3) uv: vec2<f32>"));
    ok(UBER_SHADER_SOURCE.includes("@location(4) materialIndices: vec4<f32>"));
    ok(UBER_SHADER_SOURCE.includes("@location(4) @interpolate(flat) materialIndices: vec4<f32>"));
    ok(UBER_SHADER_SOURCE.includes("@location(5) materialWeights: vec4<f32>"));
  });

  it("contains the static model vertex layout contract", () => {
    ok(UBER_SHADER_SOURCE.includes("fn modelVertexMain"));
    ok(UBER_SHADER_SOURCE.includes("struct ModelVertexInput"));
    ok(UBER_SHADER_SOURCE.includes("@location(0) position: vec3<f32>"));
    ok(UBER_SHADER_SOURCE.includes("@location(1) normal: vec3<f32>"));
    ok(UBER_SHADER_SOURCE.includes("@location(2) uv: vec2<f32>"));
    ok(UBER_SHADER_SOURCE.includes("@location(3) color: vec4<f32>"));
    ok(UBER_SHADER_SOURCE.includes("output.materialWeights = vec4<f32>(1.0, 0.0, 0.0, 0.0)"));
  });

  it("contains the basic material lighting contract", () => {
    ok(UBER_SHADER_SOURCE.includes("eyeWorld: vec4<f32>"));
    ok(UBER_SHADER_SOURCE.includes("sunDirectionAndIntensity: vec4<f32>"));
    ok(UBER_SHADER_SOURCE.includes("sunColorAndAmbient: vec4<f32>"));
    ok(UBER_SHADER_SOURCE.includes("albedoFactor: vec4<f32>"));
    ok(UBER_SHADER_SOURCE.includes("specularAndFactor: vec4<f32>"));
    ok(UBER_SHADER_SOURCE.includes("textureOptions: vec4<f32>"));
    ok(UBER_SHADER_SOURCE.includes("normalWorld: mat4x4<f32>"));
    ok(UBER_SHADER_SOURCE.includes("var albedoTexture: texture_2d_array<f32>"));
    ok(UBER_SHADER_SOURCE.includes("var normalTexture: texture_2d_array<f32>"));
    ok(UBER_SHADER_SOURCE.includes("var materialTexture: texture_2d_array<f32>"));
    ok(UBER_SHADER_SOURCE.includes("fn sampleAlbedo"));
    ok(UBER_SHADER_SOURCE.includes("fn sampleRoughness"));
    ok(UBER_SHADER_SOURCE.includes("textureSample(albedoTexture, albedoSampler"));
    ok(UBER_SHADER_SOURCE.includes("input.worldNormal"));
    ok(UBER_SHADER_SOURCE.includes("pow(max(dot(normal, halfDirection), 0.0), shininess)"));
  });

  it("contains the triplanar terrain sampling contract", () => {
    ok(UBER_SHADER_SOURCE.includes("MATERIAL_FLAG_TRIPLANAR_ALBEDO"));
    ok(UBER_SHADER_SOURCE.includes("fn sampleTriplanarTerrainAlbedoLayer"));
    ok(UBER_SHADER_SOURCE.includes("input.materialIndices"));
    ok(UBER_SHADER_SOURCE.includes("input.materialWeights"));
    ok(UBER_SHADER_SOURCE.includes("worldPosition.zy * textureScale"));
    ok(UBER_SHADER_SOURCE.includes("worldPosition.xz * textureScale"));
    ok(UBER_SHADER_SOURCE.includes("worldPosition.xy * textureScale"));
  });

  it("does not flip lighting normals based on the camera view", () => {
    ok(!UBER_SHADER_SOURCE.includes("normal = -normal"));
    ok(!UBER_SHADER_SOURCE.includes("dot(normal, viewDirection) < 0.0"));
  });

  it("contains the procedural sky contract", () => {
    ok(UBER_SHADER_SOURCE.includes("fn skyVertexMain"));
    ok(UBER_SHADER_SOURCE.includes("fn skyFragmentMain"));
    ok(UBER_SHADER_SOURCE.includes("inverseViewProjection"));
    ok(UBER_SHADER_SOURCE.includes("sunDisk"));
  });
});
