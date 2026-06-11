// Sea-level water composite shader.
// Terrain workers provide node-local bathymetry tiles. This shader first copies
// the opaque scene into the post-process targets, then draws sea-level water
// planes whose fragments sample their assigned bathymetry atlas tile.

struct Camera {
  viewProjection: mat4x4<f32>,
  inverseViewProjection: mat4x4<f32>,
  eyeWorld: vec4<f32>,
  sunDirectionAndIntensity: vec4<f32>,
  sunColorAndAmbient: vec4<f32>,
  skyTimeAndLight: vec4<f32>,
  skyAtmosphereAndCloud: vec4<f32>,
  skyCloudAndNight: vec4<f32>,
};

struct WaterUniforms {
  settings: vec4<f32>,
  depthAndDebug: vec4<f32>,
  absorption: vec4<f32>,
  shallowColor: vec4<f32>,
  deepColor: vec4<f32>,
  wavesAndReflection: vec4<f32>,
  bathymetry: vec4<f32>,
  viewport: vec4<f32>,
  reflectionViewProjection: mat4x4<f32>,
};

struct WaterCopyVertexOutput {
  @builtin(position) position: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

struct WaterPatchVertexInput {
  @location(0) originAndSpan: vec4<f32>,
  @location(1) tileAndDepth: vec4<f32>,
  @location(2) seaAndPadding: vec4<f32>,
};

struct WaterPatchVertexOutput {
  @builtin(position) position: vec4<f32>,
  @location(0) waterWorld: vec3<f32>,
  @location(1) atlasPixel: vec2<f32>,
  @location(2) @interpolate(flat) tileAndDepth: vec4<f32>,
};

struct WaterFragmentOutput {
  @location(0) color: vec4<f32>,
  @location(1) linearDepth: f32,
};

@group(0) @binding(0) var<uniform> camera: Camera;
@group(1) @binding(0) var opaqueColorTexture: texture_2d<f32>;
@group(1) @binding(1) var opaqueLinearDepthTexture: texture_2d<f32>;
@group(1) @binding(2) var bathymetryTexture: texture_2d<f32>;
@group(1) @binding(3) var reflectionColorTexture: texture_2d<f32>;
@group(1) @binding(4) var waterSampler: sampler;
@group(1) @binding(5) var<uniform> water: WaterUniforms;

const WATER_DEBUG_FINAL: f32 = 0.0;
const WATER_DEBUG_BOTTOM_DEPTH: f32 = 1.0;
const WATER_DEBUG_PATH_LENGTH: f32 = 2.0;
const WATER_DEBUG_FRESNEL: f32 = 3.0;
const WATER_DEBUG_REFLECTION: f32 = 4.0;

@vertex
fn waterCopyVertexMain(@builtin(vertex_index) vertexIndex: u32) -> WaterCopyVertexOutput {
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

  var output: WaterCopyVertexOutput;
  output.position = vec4<f32>(position, 0.0, 1.0);
  output.uv = vec2<f32>(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
  return output;
}

@vertex
fn waterPatchVertexMain(
  input: WaterPatchVertexInput,
  @builtin(vertex_index) vertexIndex: u32
) -> WaterPatchVertexOutput {
  let corner = waterPatchCorner(vertexIndex);
  let origin = input.originAndSpan.xy;
  let span = input.originAndSpan.zw;
  let worldXZ = origin + corner * span;
  let tileOrigin = input.tileAndDepth.xy;
  let tileTexels = max(input.tileAndDepth.z, 1.0);

  var output: WaterPatchVertexOutput;
  output.waterWorld = vec3<f32>(worldXZ.x, water.settings.z, worldXZ.y);
  output.atlasPixel = tileOrigin + corner * max(tileTexels - 1.0, 0.0);
  output.tileAndDepth = input.tileAndDepth;
  output.position = camera.viewProjection * vec4<f32>(output.waterWorld, 1.0);
  return output;
}

@fragment
fn waterCopyFragmentMain(input: WaterCopyVertexOutput) -> WaterFragmentOutput {
  var output: WaterFragmentOutput;
  output.color = textureSample(opaqueColorTexture, waterSampler, input.uv);
  output.linearDepth = loadOpaqueDepth(input.position.xy);
  return output;
}

@fragment
fn waterPatchFragmentMain(input: WaterPatchVertexOutput) -> WaterFragmentOutput {
  if (water.settings.x < 0.5) {
    discard;
  }

  let opaqueUv = input.position.xy * water.viewport.zw;
  let opaqueColor = textureSample(opaqueColorTexture, waterSampler, opaqueUv);
  let opaqueDepth = loadOpaqueDepth(input.position.xy);
  let rayDirection = normalize(input.waterWorld - camera.eyeWorld.xyz);
  let waterDistance = length(input.waterWorld - camera.eyeWorld.xyz);
  if (opaqueDepth > 0.0 && opaqueDepth <= waterDistance) {
    discard;
  }

  let tileOrigin = input.tileAndDepth.xy;
  let tileTexels = input.tileAndDepth.z;
  let bottomDepth = loadBathymetryDepth(input.atlasPixel, tileOrigin, tileTexels);
  if (bottomDepth <= 0.03) {
    discard;
  }
  let bottomGradient = bathymetryGradient(input.atlasPixel, tileOrigin, tileTexels);

  let pathLength = select(
    water.depthAndDebug.z,
    max(opaqueDepth - waterDistance, 0.0),
    opaqueDepth > waterDistance
  );
  let normal = waveNormal(input.waterWorld.xz, water.settings.w);
  let foam = foamAmount(bottomDepth, pathLength, bottomGradient, input.waterWorld.xz, water.settings.w);
  let viewDirection = normalize(camera.eyeWorld.xyz - input.waterWorld);
  let fresnel = 0.02 + 0.98 * pow(1.0 - saturate(dot(normal, viewDirection)), 5.0);
  let reflected = reflectionColor(input.waterWorld, normal);
  let maybeDebug = debugColor(bottomDepth, pathLength, fresnel, reflected);

  var output: WaterFragmentOutput;
  if (maybeDebug.x >= 0.0) {
    output.color = vec4<f32>(maybeDebug, 1.0);
    output.linearDepth = waterDistance;
    return output;
  }

  let color = waterSurfaceColor(
    opaqueColor.rgb,
    input.waterWorld,
    normal,
    rayDirection,
    bottomDepth,
    pathLength,
    fresnel,
    foam
  );
  output.color = vec4<f32>(max(color, vec3<f32>(0.0)), 1.0);
  output.linearDepth = waterDistance;
  return output;
}

fn waterPatchCorner(vertexIndex: u32) -> vec2<f32> {
  switch vertexIndex {
    case 0u: {
      return vec2<f32>(0.0, 0.0);
    }
    case 1u: {
      return vec2<f32>(1.0, 0.0);
    }
    case 2u: {
      return vec2<f32>(0.0, 1.0);
    }
    case 3u: {
      return vec2<f32>(0.0, 1.0);
    }
    case 4u: {
      return vec2<f32>(1.0, 0.0);
    }
    default: {
      return vec2<f32>(1.0, 1.0);
    }
  }
}

fn saturate(value: f32) -> f32 {
  return clamp(value, 0.0, 1.0);
}

fn loadOpaqueDepth(pixelPosition: vec2<f32>) -> f32 {
  let dimensions = textureDimensions(opaqueLinearDepthTexture);
  let pixel = clamp(
    vec2<i32>(floor(pixelPosition)),
    vec2<i32>(0),
    vec2<i32>(dimensions) - vec2<i32>(1)
  );
  return textureLoad(opaqueLinearDepthTexture, pixel, 0).r;
}

fn loadBathymetryDepth(atlasPixel: vec2<f32>, tileOrigin: vec2<f32>, tileTexels: f32) -> f32 {
  let tileMax = tileOrigin + vec2<f32>(max(tileTexels - 1.0, 0.0));
  let pixel = clamp(atlasPixel, tileOrigin, tileMax);
  let basePixel = floor(pixel);
  let blend = pixel - basePixel;
  let nextPixel = min(basePixel + vec2<f32>(1.0), tileMax);

  let depth00 = loadBathymetryTexel(basePixel);
  let depth10 = loadBathymetryTexel(vec2<f32>(nextPixel.x, basePixel.y));
  let depth01 = loadBathymetryTexel(vec2<f32>(basePixel.x, nextPixel.y));
  let depth11 = loadBathymetryTexel(nextPixel);

  return mix(mix(depth00, depth10, blend.x), mix(depth01, depth11, blend.x), blend.y);
}

fn loadBathymetryTexel(atlasPixel: vec2<f32>) -> f32 {
  let dimensions = textureDimensions(bathymetryTexture);
  let pixel = clamp(
    vec2<i32>(floor(atlasPixel)),
    vec2<i32>(0),
    vec2<i32>(dimensions) - vec2<i32>(1)
  );
  return textureLoad(bathymetryTexture, pixel, 0).r;
}

fn bathymetryGradient(atlasPixel: vec2<f32>, tileOrigin: vec2<f32>, tileTexels: f32) -> vec2<f32> {
  let left = loadBathymetryDepth(atlasPixel + vec2<f32>(-1.0, 0.0), tileOrigin, tileTexels);
  let right = loadBathymetryDepth(atlasPixel + vec2<f32>(1.0, 0.0), tileOrigin, tileTexels);
  let down = loadBathymetryDepth(atlasPixel + vec2<f32>(0.0, -1.0), tileOrigin, tileTexels);
  let up = loadBathymetryDepth(atlasPixel + vec2<f32>(0.0, 1.0), tileOrigin, tileTexels);
  return vec2<f32>(right - left, up - down) * 0.5;
}

fn waveNormal(worldXZ: vec2<f32>, timeSeconds: f32) -> vec3<f32> {
  let scale = max(water.wavesAndReflection.x, 0.0001);
  let strength = water.wavesAndReflection.y;
  let p = worldXZ * scale;
  var slope = vec2<f32>(0.0);
  slope += rippleSlope(p, normalize(vec2<f32>(1.0, 0.31)), 1.15, 1.05, timeSeconds, 0.45);
  slope += rippleSlope(p, normalize(vec2<f32>(-0.48, 1.0)), 2.10, -0.82, timeSeconds, 0.28);
  slope += rippleSlope(p, normalize(vec2<f32>(0.72, 1.0)), 3.80, 1.58, timeSeconds, 0.18);
  slope += rippleSlope(p, normalize(vec2<f32>(-1.0, 0.18)), 6.20, -2.10, timeSeconds, 0.10);
  return normalize(vec3<f32>(-slope.x * strength, 1.0, -slope.y * strength));
}

fn rippleSlope(
  p: vec2<f32>,
  direction: vec2<f32>,
  frequency: f32,
  speed: f32,
  timeSeconds: f32,
  weight: f32
) -> vec2<f32> {
  let phase = dot(p, direction) * frequency + timeSeconds * speed;
  return direction * cos(phase) * frequency * weight;
}

fn reflectionUv(waterWorld: vec3<f32>, normal: vec3<f32>) -> vec2<f32> {
  let clip = water.reflectionViewProjection * vec4<f32>(waterWorld, 1.0);
  let ndc = clip.xy / max(abs(clip.w), 0.0001);
  let baseUv = vec2<f32>(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
  let distortion = normal.xz * 0.018 * water.wavesAndReflection.y;
  return clamp(baseUv + distortion, vec2<f32>(0.001), vec2<f32>(0.999));
}

fn reflectionColor(waterWorld: vec3<f32>, normal: vec3<f32>) -> vec3<f32> {
  if (water.settings.y < 0.5) {
    return vec3<f32>(0.0);
  }
  return textureSampleLevel(
    reflectionColorTexture,
    waterSampler,
    reflectionUv(waterWorld, normal),
    0.0
  ).rgb;
}

fn absorptionAmount(pathLength: f32) -> vec3<f32> {
  return exp(-max(water.absorption.rgb, vec3<f32>(0.0)) * max(pathLength, 0.0));
}

fn foamAmount(
  bottomDepth: f32,
  pathLength: f32,
  bottomGradient: vec2<f32>,
  worldXZ: vec2<f32>,
  timeSeconds: f32
) -> f32 {
  let shallowDepth = max(water.depthAndDebug.x, 0.001);
  let shoreBand = 1.0 - smoothstep(0.06, shallowDepth * 0.95, bottomDepth);
  let thinWaterBand = smoothstep(0.035, 0.16, bottomDepth) *
    (1.0 - smoothstep(shallowDepth * 0.65, shallowDepth * 1.15, bottomDepth));
  let slopeBand = smoothstep(0.08, 0.55, length(bottomGradient));
  let pathFade = saturate(pathLength * 0.65 + 0.18);
  let driftA = worldXZ * 0.16 + vec2<f32>(timeSeconds * 0.035, -timeSeconds * 0.025);
  let driftB = worldXZ * 0.34 + vec2<f32>(-timeSeconds * 0.050, timeSeconds * 0.040);
  let lace = fbmNoise(driftA) * 0.62 + valueNoise(driftB) * 0.38;
  let brokenLace = smoothstep(0.58, 0.94, lace + shoreBand * 0.10 + slopeBand * 0.20);
  return saturate(thinWaterBand * slopeBand * brokenLace * pathFade * 0.9);
}

fn hash21(p: vec2<f32>) -> f32 {
  return fract(sin(dot(p, vec2<f32>(127.1, 311.7))) * 43758.5453123);
}

fn valueNoise(p: vec2<f32>) -> f32 {
  let cell = floor(p);
  let local = fract(p);
  let smoothLocal = local * local * (vec2<f32>(3.0) - 2.0 * local);
  let a = hash21(cell);
  let b = hash21(cell + vec2<f32>(1.0, 0.0));
  let c = hash21(cell + vec2<f32>(0.0, 1.0));
  let d = hash21(cell + vec2<f32>(1.0, 1.0));
  return mix(mix(a, b, smoothLocal.x), mix(c, d, smoothLocal.x), smoothLocal.y);
}

fn fbmNoise(p: vec2<f32>) -> f32 {
  var value = 0.0;
  var amplitude = 0.5;
  var frequency = 1.0;
  var samplePoint = p;
  for (var octave = 0; octave < 4; octave = octave + 1) {
    value += valueNoise(samplePoint * frequency) * amplitude;
    samplePoint = mat2x2<f32>(1.62, 1.10, -1.10, 1.62) * samplePoint + vec2<f32>(13.7, -8.3);
    frequency *= 1.9;
    amplitude *= 0.5;
  }
  return value;
}

fn waterSurfaceColor(
  opaqueColor: vec3<f32>,
  waterWorld: vec3<f32>,
  normal: vec3<f32>,
  rayDirection: vec3<f32>,
  bottomDepth: f32,
  pathLength: f32,
  fresnel: f32,
  foam: f32
) -> vec3<f32> {
  let shallowDepth = max(water.depthAndDebug.x, 0.001);
  let deepDepth = max(water.depthAndDebug.y, shallowDepth + 0.001);
  let depthFactor = smoothstep(shallowDepth, deepDepth, bottomDepth);
  let waterTint = mix(water.shallowColor.rgb, water.deepColor.rgb, depthFactor);
  let transmittance = absorptionAmount(pathLength);
  let absorbedBottom = opaqueColor * transmittance;
  let pathDensity = saturate(1.0 - dot(transmittance, vec3<f32>(0.3333)));
  let shallowPresence = smoothstep(0.03, shallowDepth * 1.35, bottomDepth);
  let edgeDensityFloor = 0.16 + shallowPresence * (0.20 + depthFactor * 0.22);
  let density = saturate(max(pathDensity * (0.72 + depthFactor * 0.38), edgeDensityFloor));
  var volumeColor = mix(absorbedBottom, waterTint, density);

  let viewDirection = normalize(-rayDirection);
  let lightDirection = normalize(camera.sunDirectionAndIntensity.xyz);
  let halfVector = normalize(lightDirection + viewDirection);
  let nDotL = saturate(dot(normal, lightDirection));
  let specularPower = mix(180.0, 46.0, saturate(water.wavesAndReflection.y));
  let sunGlitter = pow(saturate(dot(normal, halfVector)), specularPower) *
    nDotL *
    camera.sunDirectionAndIntensity.w;
  let reflected = reflectionColor(waterWorld, normal);
  let reflectionMix = fresnel * water.settings.y;
  let reflectedColor = mix(volumeColor, reflected, reflectionMix);
  let foamColor = vec3<f32>(0.82, 0.94, 0.96);
  let foamedColor = mix(reflectedColor, foamColor, foam * (0.60 + fresnel * 0.25));

  return foamedColor + camera.sunColorAndAmbient.rgb * sunGlitter * (0.10 + fresnel * 0.48);
}

fn debugColor(bottomDepth: f32, pathLength: f32, fresnel: f32, reflection: vec3<f32>) -> vec3<f32> {
  let debugView = water.depthAndDebug.w;
  if (abs(debugView - WATER_DEBUG_BOTTOM_DEPTH) < 0.5) {
    return vec3<f32>(saturate(bottomDepth / max(water.depthAndDebug.y, 0.001)));
  }
  if (abs(debugView - WATER_DEBUG_PATH_LENGTH) < 0.5) {
    return vec3<f32>(saturate(pathLength / max(water.depthAndDebug.z, 0.001)));
  }
  if (abs(debugView - WATER_DEBUG_FRESNEL) < 0.5) {
    return vec3<f32>(fresnel);
  }
  if (abs(debugView - WATER_DEBUG_REFLECTION) < 0.5) {
    return max(reflection, vec3<f32>(0.0));
  }
  return vec3<f32>(-1.0);
}
