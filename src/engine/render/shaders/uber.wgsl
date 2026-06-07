// Shared WebGPU terrain and model shader used by the Rust-owned renderer.
// This file contains the main color pass plus the CSM depth/debug shader entry
// points so the browser and native smoke paths exercise the same WGSL contract.

struct Camera {
  viewProjection: mat4x4<f32>,
  inverseViewProjection: mat4x4<f32>,
  eyeWorld: vec4<f32>,
  sunDirectionAndIntensity: vec4<f32>,
  sunColorAndAmbient: vec4<f32>,
};

struct ObjectUniforms {
  world: mat4x4<f32>,
  normalWorld: mat4x4<f32>,
  albedoFactor: vec4<f32>,
  specularAndFactor: vec4<f32>,
  textureOptions: vec4<f32>,
};

struct Shadows {
  lightViewProjection0: mat4x4<f32>,
  lightViewProjection1: mat4x4<f32>,
  lightViewProjection2: mat4x4<f32>,
  lightViewProjection3: mat4x4<f32>,
  cascadeSplits: vec4<f32>,
  options: vec4<f32>,
  spare: vec4<f32>,
};

@group(0) @binding(0) var<uniform> camera: Camera;
@group(1) @binding(0) var<uniform> object: ObjectUniforms;
@group(1) @binding(1) var albedoTexture: texture_2d_array<f32>;
@group(1) @binding(2) var normalTexture: texture_2d_array<f32>;
@group(1) @binding(3) var materialTexture: texture_2d_array<f32>;
@group(1) @binding(4) var albedoSampler: sampler;
@group(2) @binding(0) var<uniform> shadows: Shadows;
@group(2) @binding(1) var shadowTexture: texture_depth_2d_array;
@group(2) @binding(2) var shadowSampler: sampler_comparison;

const PI: f32 = 3.14159265359;
const MATERIAL_WORKFLOW_TERRAIN: f32 = 1.0;
const MATERIAL_WORKFLOW_METALLIC_ROUGHNESS: f32 = 2.0;
const MATERIAL_WORKFLOW_SPECULAR_GLOSSINESS: f32 = 3.0;
const SHADOW_DEBUG_OFF: i32 = 0;
const SHADOW_DEBUG_CASCADE_INDEX: i32 = 1;
const SHADOW_DEBUG_VISIBILITY: i32 = 2;
const SHADOW_DEBUG_DEPTH_CASCADE0: i32 = 3;

struct VertexInput {
  @location(0) position: vec3<f32>,
  @location(1) color: vec3<f32>,
  @location(2) normal: vec3<f32>,
  @location(3) uv: vec2<f32>,
  @location(4) materialIndices: vec4<f32>,
  @location(5) materialWeights: vec4<f32>,
};

struct ModelVertexInput {
  @location(0) position: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) uv: vec2<f32>,
  @location(3) color: vec4<f32>,
};

struct VertexOutput {
  @builtin(position) clipPosition: vec4<f32>,
  @location(0) color: vec3<f32>,
  @location(1) worldPosition: vec3<f32>,
  @location(2) worldNormal: vec3<f32>,
  @location(3) uv: vec2<f32>,
  @location(4) @interpolate(flat) materialIndices: vec4<f32>,
  @location(5) materialWeights: vec4<f32>,
};

struct ShadowCoordinates {
  cascadeIndex: i32,
  uv: vec2<f32>,
  depth: f32,
  inside: bool,
};

@vertex
fn vertexMain(input: VertexInput) -> VertexOutput {
  var output: VertexOutput;
  let worldPosition = object.world * vec4<f32>(input.position, 1.0);
  output.clipPosition = camera.viewProjection * worldPosition;
  output.color = input.color;
  output.worldPosition = worldPosition.xyz;
  output.worldNormal = normalize((object.normalWorld * vec4<f32>(input.normal, 0.0)).xyz);
  output.uv = input.uv;
  output.materialIndices = input.materialIndices;
  output.materialWeights = input.materialWeights;
  return output;
}

@vertex
fn modelVertexMain(input: ModelVertexInput) -> VertexOutput {
  var output: VertexOutput;
  let worldPosition = object.world * vec4<f32>(input.position, 1.0);
  output.clipPosition = camera.viewProjection * worldPosition;
  output.color = input.color.rgb;
  output.worldPosition = worldPosition.xyz;
  output.worldNormal = normalize((object.normalWorld * vec4<f32>(input.normal, 0.0)).xyz);
  output.uv = input.uv;
  output.materialIndices = vec4<f32>(0.0, 0.0, 0.0, 0.0);
  output.materialWeights = vec4<f32>(1.0, 0.0, 0.0, 0.0);
  return output;
}

@vertex
fn shadowVertexMain(input: VertexInput) -> @builtin(position) vec4<f32> {
  let worldPosition = object.world * vec4<f32>(input.position, 1.0);
  return shadows.lightViewProjection0 * worldPosition;
}

@vertex
fn shadowModelVertexMain(input: ModelVertexInput) -> @builtin(position) vec4<f32> {
  let worldPosition = object.world * vec4<f32>(input.position, 1.0);
  return shadows.lightViewProjection0 * worldPosition;
}

fn shadowDebugMode() -> i32 {
  return i32(round(shadows.spare.x));
}

fn shadowCascadeIndex(viewDepth: f32) -> i32 {
  if (viewDepth <= shadows.cascadeSplits.x) {
    return 0;
  }
  if (viewDepth <= shadows.cascadeSplits.y) {
    return 1;
  }
  if (viewDepth <= shadows.cascadeSplits.z) {
    return 2;
  }
  return 3;
}

fn shadowMatrixForCascade(cascadeIndex: i32) -> mat4x4<f32> {
  if (cascadeIndex == 0) {
    return shadows.lightViewProjection0;
  }
  if (cascadeIndex == 1) {
    return shadows.lightViewProjection1;
  }
  if (cascadeIndex == 2) {
    return shadows.lightViewProjection2;
  }
  return shadows.lightViewProjection3;
}

fn shadowCoordinatesForCascade(worldPosition: vec3<f32>, cascadeIndex: i32) -> ShadowCoordinates {
  let lightPosition = shadowMatrixForCascade(cascadeIndex) * vec4<f32>(worldPosition, 1.0);
  var result: ShadowCoordinates;
  result.cascadeIndex = cascadeIndex;
  result.uv = vec2<f32>(0.0);
  result.depth = 1.0;
  result.inside = false;
  if (abs(lightPosition.w) <= 0.00001) {
    return result;
  }

  let ndc = lightPosition.xyz / lightPosition.w;
  result.uv = vec2<f32>(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
  result.depth = ndc.z;
  result.inside =
    result.uv.x >= 0.0 && result.uv.x <= 1.0 &&
    result.uv.y >= 0.0 && result.uv.y <= 1.0 &&
    result.depth >= 0.0 && result.depth <= 1.0;
  return result;
}

fn shadowCoordinates(worldPosition: vec3<f32>) -> ShadowCoordinates {
  let viewDepth = length(worldPosition - camera.eyeWorld.xyz);
  return shadowCoordinatesForCascade(worldPosition, shadowCascadeIndex(viewDepth));
}

fn loadShadowDepth(cascadeIndex: i32, uv: vec2<f32>) -> f32 {
  let dimensions = textureDimensions(shadowTexture);
  let clampedUv = clamp(uv, vec2<f32>(0.0), vec2<f32>(0.999999));
  let texel = vec2<i32>(
    i32(clampedUv.x * f32(dimensions.x)),
    i32(clampedUv.y * f32(dimensions.y))
  );
  return textureLoad(shadowTexture, texel, cascadeIndex, 0);
}

fn sampleShadowVisibility(worldPosition: vec3<f32>) -> f32 {
  if (shadows.options.x < 0.5) {
    return 1.0;
  }
  let coords = shadowCoordinates(worldPosition);
  if (!coords.inside) {
    return 1.0;
  }

  let depthReference = coords.depth - shadows.options.y;
  let texelSize = shadows.options.w;
  var visibility = 0.0;
  for (var y = -1; y <= 1; y = y + 1) {
    for (var x = -1; x <= 1; x = x + 1) {
      let offset = vec2<f32>(f32(x), f32(y)) * texelSize;
      visibility += textureSampleCompareLevel(
        shadowTexture,
        shadowSampler,
        coords.uv + offset,
        coords.cascadeIndex,
        depthReference
      );
    }
  }
  return visibility / 9.0;
}

fn shadowCascadeDebugColor(cascadeIndex: i32) -> vec3<f32> {
  if (cascadeIndex == 0) {
    return vec3<f32>(0.96, 0.18, 0.16);
  }
  if (cascadeIndex == 1) {
    return vec3<f32>(0.12, 0.72, 0.25);
  }
  if (cascadeIndex == 2) {
    return vec3<f32>(0.18, 0.42, 1.0);
  }
  return vec3<f32>(1.0, 0.82, 0.16);
}

fn shadowDepthDebugColor(worldPosition: vec3<f32>, cascadeIndex: i32) -> vec3<f32> {
  let coords = shadowCoordinatesForCascade(worldPosition, cascadeIndex);
  if (!coords.inside) {
    return vec3<f32>(0.02);
  }
  let depth = loadShadowDepth(cascadeIndex, coords.uv);
  if (depth >= 0.9999) {
    return vec3<f32>(0.0);
  }
  let value = clamp(0.08 + (1.0 - depth) * 8.0, 0.0, 1.0);
  return vec3<f32>(value);
}

fn shadowDebugColor(input: VertexOutput) -> vec4<f32> {
  let mode = shadowDebugMode();
  if (mode == SHADOW_DEBUG_CASCADE_INDEX) {
    let coords = shadowCoordinates(input.worldPosition);
    return vec4<f32>(shadowCascadeDebugColor(coords.cascadeIndex), 1.0);
  }
  if (mode == SHADOW_DEBUG_VISIBILITY) {
    let visibility = sampleShadowVisibility(input.worldPosition);
    return vec4<f32>(vec3<f32>(visibility), 1.0);
  }
  if (mode >= SHADOW_DEBUG_DEPTH_CASCADE0) {
    return vec4<f32>(
      shadowDepthDebugColor(input.worldPosition, clamp(mode - SHADOW_DEBUG_DEPTH_CASCADE0, 0, 3)),
      1.0
    );
  }
  return vec4<f32>(0.0, 0.0, 0.0, 1.0);
}

fn sampleTerrainAlbedoLayer(uv: vec2<f32>, layer: f32) -> vec3<f32> {
  return textureSample(albedoTexture, albedoSampler, fract(uv), i32(round(layer))).rgb;
}

fn sampleTerrainMaterialLayer(uv: vec2<f32>, layer: f32) -> vec4<f32> {
  return textureSample(materialTexture, albedoSampler, fract(uv), i32(round(layer)));
}

fn sampleTriplanarTerrainAlbedoLayer(
  worldPosition: vec3<f32>,
  normal: vec3<f32>,
  layer: f32
) -> vec3<f32> {
  var weights = abs(normal);
  weights = weights * weights;
  weights = weights * weights;
  weights = weights / max(weights.x + weights.y + weights.z, 0.0001);

  let textureScale = object.textureOptions.y;
  let xSample = sampleTerrainAlbedoLayer(worldPosition.zy * textureScale, layer);
  let ySample = sampleTerrainAlbedoLayer(worldPosition.xz * textureScale, layer);
  let zSample = sampleTerrainAlbedoLayer(worldPosition.xy * textureScale, layer);

  return xSample * weights.x + ySample * weights.y + zSample * weights.z;
}

fn sampleTriplanarTerrainRoughnessLayer(
  worldPosition: vec3<f32>,
  normal: vec3<f32>,
  layer: f32
) -> f32 {
  var weights = abs(normal);
  weights = weights * weights;
  weights = weights * weights;
  weights = weights / max(weights.x + weights.y + weights.z, 0.0001);

  let textureScale = object.textureOptions.y;
  let xSample = sampleTerrainMaterialLayer(worldPosition.zy * textureScale, layer).r;
  let ySample = sampleTerrainMaterialLayer(worldPosition.xz * textureScale, layer).r;
  let zSample = sampleTerrainMaterialLayer(worldPosition.xy * textureScale, layer).r;

  return xSample * weights.x + ySample * weights.y + zSample * weights.z;
}

fn materialWorkflowIs(workflow: f32) -> bool {
  return abs(object.textureOptions.x - workflow) < 0.5;
}

fn srgbToLinear(color: vec3<f32>) -> vec3<f32> {
  return pow(max(color, vec3<f32>(0.0)), vec3<f32>(2.2));
}

fn linearToSrgb(color: vec3<f32>) -> vec3<f32> {
  return pow(max(color, vec3<f32>(0.0)), vec3<f32>(1.0 / 2.2));
}

fn terrainWeights(input: VertexOutput) -> vec4<f32> {
  return input.materialWeights / max(
    input.materialWeights.x + input.materialWeights.y + input.materialWeights.z + input.materialWeights.w,
    0.0001
  );
}

fn sampleTerrainAlbedo(input: VertexOutput, normal: vec3<f32>) -> vec3<f32> {
  let weights = terrainWeights(input);
  return
    sampleTriplanarTerrainAlbedoLayer(input.worldPosition, normal, input.materialIndices.x) * weights.x +
    sampleTriplanarTerrainAlbedoLayer(input.worldPosition, normal, input.materialIndices.y) * weights.y +
    sampleTriplanarTerrainAlbedoLayer(input.worldPosition, normal, input.materialIndices.z) * weights.z +
    sampleTriplanarTerrainAlbedoLayer(input.worldPosition, normal, input.materialIndices.w) * weights.w;
}

fn sampleTerrainRoughness(input: VertexOutput, normal: vec3<f32>) -> f32 {
  let weights = terrainWeights(input);
  return
    sampleTriplanarTerrainRoughnessLayer(input.worldPosition, normal, input.materialIndices.x) * weights.x +
    sampleTriplanarTerrainRoughnessLayer(input.worldPosition, normal, input.materialIndices.y) * weights.y +
    sampleTriplanarTerrainRoughnessLayer(input.worldPosition, normal, input.materialIndices.z) * weights.z +
    sampleTriplanarTerrainRoughnessLayer(input.worldPosition, normal, input.materialIndices.w) * weights.w;
}

fn sampleModelBaseColor(input: VertexOutput) -> vec4<f32> {
  let textureColor = textureSample(albedoTexture, albedoSampler, input.uv, 0);
  return vec4<f32>(
    input.color * object.albedoFactor.rgb * srgbToLinear(textureColor.rgb),
    object.albedoFactor.a * textureColor.a
  );
}

fn sampleModelMetallicRoughness(input: VertexOutput) -> vec2<f32> {
  let packed = textureSample(materialTexture, albedoSampler, input.uv, 0);
  let metallic = clamp(object.specularAndFactor.x * packed.b, 0.0, 1.0);
  let roughness = clamp(object.specularAndFactor.y * packed.g, 0.04, 1.0);
  return vec2<f32>(metallic, roughness);
}

fn sampleModelSpecularGlossiness(input: VertexOutput) -> vec4<f32> {
  let packed = textureSample(materialTexture, albedoSampler, input.uv, 0);
  return vec4<f32>(
    clamp(object.specularAndFactor.rgb * packed.rgb, vec3<f32>(0.0), vec3<f32>(1.0)),
    clamp(object.specularAndFactor.w * packed.a, 0.0, 1.0)
  );
}

fn pbrDirectLight(
  diffuseColor: vec3<f32>,
  f0: vec3<f32>,
  perceptualRoughness: f32,
  normal: vec3<f32>,
  viewDirection: vec3<f32>,
  shadowVisibility: f32
) -> vec3<f32> {
  let lightDirection = normalize(camera.sunDirectionAndIntensity.xyz);
  let halfDirection = normalize(lightDirection + viewDirection);
  let nDotL = max(dot(normal, lightDirection), 0.0);
  let nDotV = max(dot(normal, viewDirection), 0.001);
  let nDotH = max(dot(normal, halfDirection), 0.0);
  let vDotH = max(dot(viewDirection, halfDirection), 0.0);
  let alpha = max(perceptualRoughness * perceptualRoughness, 0.001);
  let alphaSquared = alpha * alpha;
  let dDenom = max((nDotH * nDotH) * (alphaSquared - 1.0) + 1.0, 0.001);
  let distribution = alphaSquared / max(PI * dDenom * dDenom, 0.001);
  let k = ((perceptualRoughness + 1.0) * (perceptualRoughness + 1.0)) / 8.0;
  let geometryL = nDotL / max(nDotL * (1.0 - k) + k, 0.001);
  let geometryV = nDotV / max(nDotV * (1.0 - k) + k, 0.001);
  let geometry = geometryL * geometryV;
  let fresnel = f0 + (vec3<f32>(1.0) - f0) * pow(1.0 - vDotH, 5.0);
  let specular = distribution * geometry * fresnel / max(4.0 * nDotL * nDotV, 0.001);
  let diffuse = diffuseColor / PI;
  let direct =
    (diffuse + specular) *
    camera.sunColorAndAmbient.rgb *
    camera.sunDirectionAndIntensity.w *
    nDotL *
    shadowVisibility;
  let ambient = diffuseColor * camera.sunColorAndAmbient.w;
  return ambient + direct;
}

fn shadeTerrain(input: VertexOutput, normal: vec3<f32>, viewDirection: vec3<f32>) -> vec4<f32> {
  let lightDirection = normalize(camera.sunDirectionAndIntensity.xyz);
  let halfDirection = normalize(lightDirection + viewDirection);
  let diffuse = max(dot(normal, lightDirection), 0.0) * camera.sunDirectionAndIntensity.w;
  let roughness = clamp(sampleTerrainRoughness(input, normal), 0.04, 1.0);
  let shininess = mix(96.0, 10.0, roughness);
  let specular = pow(max(dot(normal, halfDirection), 0.0), shininess) *
    object.specularAndFactor.w *
    (1.0 - roughness * 0.72) *
    camera.sunDirectionAndIntensity.w;
  let sampledAlbedo = sampleTerrainAlbedo(input, normal);
  let vertexColor = mix(vec3<f32>(1.0), input.color, 0.35);
  let albedo = vertexColor * object.albedoFactor.rgb * sampledAlbedo;
  let shadowVisibility = sampleShadowVisibility(input.worldPosition);
  let litColor =
    albedo * (camera.sunColorAndAmbient.w + diffuse * camera.sunColorAndAmbient.rgb * shadowVisibility) +
    object.specularAndFactor.rgb * camera.sunColorAndAmbient.rgb * specular * shadowVisibility;

  return vec4<f32>(litColor, object.albedoFactor.a);
}

fn shadeMetallicRoughness(input: VertexOutput, normal: vec3<f32>, viewDirection: vec3<f32>) -> vec4<f32> {
  let baseColor = sampleModelBaseColor(input);
  let metallicRoughness = sampleModelMetallicRoughness(input);
  let metallic = metallicRoughness.x;
  let roughness = metallicRoughness.y;
  let f0 = mix(vec3<f32>(0.04), baseColor.rgb, metallic);
  let diffuseColor = baseColor.rgb * (1.0 - metallic);
  let shadowVisibility = sampleShadowVisibility(input.worldPosition);
  let litColor = pbrDirectLight(diffuseColor, f0, roughness, normal, viewDirection, shadowVisibility);
  return vec4<f32>(linearToSrgb(litColor), baseColor.a);
}

fn shadeSpecularGlossiness(input: VertexOutput, normal: vec3<f32>, viewDirection: vec3<f32>) -> vec4<f32> {
  let diffuse = sampleModelBaseColor(input);
  let specularGlossiness = sampleModelSpecularGlossiness(input);
  let specular = specularGlossiness.rgb;
  let glossiness = specularGlossiness.a;
  let maxSpecular = max(specular.r, max(specular.g, specular.b));
  let diffuseColor = diffuse.rgb * (1.0 - maxSpecular);
  let roughness = clamp(1.0 - glossiness, 0.04, 1.0);
  let shadowVisibility = sampleShadowVisibility(input.worldPosition);
  let litColor = pbrDirectLight(diffuseColor, specular, roughness, normal, viewDirection, shadowVisibility);
  return vec4<f32>(linearToSrgb(litColor), diffuse.a);
}

@fragment
fn fragmentMain(input: VertexOutput) -> @location(0) vec4<f32> {
  if (shadowDebugMode() != SHADOW_DEBUG_OFF) {
    return shadowDebugColor(input);
  }

  let viewDirection = normalize(camera.eyeWorld.xyz - input.worldPosition);
  let normal = normalize(input.worldNormal);
  if (materialWorkflowIs(MATERIAL_WORKFLOW_TERRAIN)) {
    return shadeTerrain(input, normal, viewDirection);
  }
  if (materialWorkflowIs(MATERIAL_WORKFLOW_SPECULAR_GLOSSINESS)) {
    return shadeSpecularGlossiness(input, normal, viewDirection);
  }
  return shadeMetallicRoughness(input, normal, viewDirection);
}

struct SkyVertexOutput {
  @builtin(position) clipPosition: vec4<f32>,
  @location(0) ndc: vec2<f32>,
};

@vertex
fn skyVertexMain(@builtin(vertex_index) vertexIndex: u32) -> SkyVertexOutput {
  var position = vec2<f32>(-1.0, 3.0);
  switch vertexIndex {
    case 0u: {
      position = vec2<f32>(-1.0, -1.0);
    }
    case 1u: {
      position = vec2<f32>(3.0, -1.0);
    }
    default: {}
  }

  var output: SkyVertexOutput;
  output.clipPosition = vec4<f32>(position, 1.0, 1.0);
  output.ndc = position;
  return output;
}

@fragment
fn skyFragmentMain(input: SkyVertexOutput) -> @location(0) vec4<f32> {
  let farWorldH = camera.inverseViewProjection * vec4<f32>(input.ndc, 1.0, 1.0);
  let farWorld = farWorldH.xyz / farWorldH.w;
  let ray = normalize(farWorld - camera.eyeWorld.xyz);
  let sunDirection = normalize(camera.sunDirectionAndIntensity.xyz);
  let skyT = smoothstep(-0.18, 0.82, ray.y);
  let horizon = vec3<f32>(0.045, 0.075, 0.11);
  let zenith = vec3<f32>(0.36, 0.62, 0.96);
  var skyColor = mix(horizon, zenith, skyT);

  let sunDot = dot(ray, sunDirection);
  let sunGlow = smoothstep(0.9, 1.0, sunDot) * 0.3;
  let sunDisk = smoothstep(0.995, 0.9985, sunDot) * 1.75;
  skyColor += camera.sunColorAndAmbient.rgb * (sunGlow + sunDisk);

  return vec4<f32>(skyColor, 1.0);
}
