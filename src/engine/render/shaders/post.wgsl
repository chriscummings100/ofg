struct PostVertexOutput {
  @builtin(position) clipPosition: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

struct PostProcessUniforms {
  debugViewAndScale: vec4<f32>,
  bloomSettings: vec4<f32>,
  dofSettings: vec4<f32>,
  fogSettings: vec4<f32>,
  fogColorAndCurve: vec4<f32>,
};

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

@group(0) @binding(0) var sceneColorTexture: texture_2d<f32>;
@group(0) @binding(1) var linearDepthTexture: texture_2d<f32>;
@group(0) @binding(2) var postSampler: sampler;
@group(0) @binding(3) var<uniform> postProcess: PostProcessUniforms;
@group(0) @binding(4) var bloomTexture: texture_2d<f32>;

@group(1) @binding(0) var bloomSceneColorTexture: texture_2d<f32>;
@group(1) @binding(1) var bloomSampler: sampler;
@group(1) @binding(2) var<uniform> bloomPostProcess: PostProcessUniforms;

@group(2) @binding(0) var<uniform> camera: Camera;

const POST_DEBUG_FINAL: f32 = 0.0;
const POST_DEBUG_SCENE_COLOR: f32 = 1.0;
const POST_DEBUG_LINEAR_DEPTH: f32 = 2.0;
const POST_DEBUG_POST_TONE_MAP: f32 = 3.0;
const POST_DEBUG_BLOOM: f32 = 4.0;
const POST_DEBUG_DOF_COC: f32 = 5.0;
const POST_DEBUG_DOF_BLURRED: f32 = 6.0;
const POST_DEBUG_FOG_FACTOR: f32 = 7.0;

@vertex
fn postVertexMain(@builtin(vertex_index) vertexIndex: u32) -> PostVertexOutput {
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

  var output: PostVertexOutput;
  output.clipPosition = vec4<f32>(position, 0.0, 1.0);
  output.uv = vec2<f32>(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
  return output;
}

fn linearDepthAtPixel(pixelPosition: vec2<f32>) -> f32 {
  let size = textureDimensions(linearDepthTexture);
  let pixel = clamp(
    vec2<i32>(floor(pixelPosition)),
    vec2<i32>(0),
    vec2<i32>(size) - vec2<i32>(1)
  );
  return textureLoad(linearDepthTexture, pixel, 0).r;
}

fn debugLinearDepth(pixelPosition: vec2<f32>) -> vec3<f32> {
  let linearDepth = linearDepthAtPixel(pixelPosition);
  let scale = postProcess.debugViewAndScale.y;
  let mapped = 1.0 - exp(-max(linearDepth, 0.0) * scale);
  return vec3<f32>(mapped);
}

fn acesFilmic(color: vec3<f32>) -> vec3<f32> {
  let a = 2.51;
  let b = 0.03;
  let c = 2.43;
  let d = 0.59;
  let e = 0.14;
  return clamp((color * (a * color + b)) / (color * (c * color + d) + e), vec3<f32>(0.0), vec3<f32>(1.0));
}

fn applyToneMap(sceneColor: vec3<f32>) -> vec3<f32> {
  let exposedColor = max(sceneColor * postProcess.debugViewAndScale.z, vec3<f32>(0.0));
  if (postProcess.debugViewAndScale.w < 0.5) {
    return clamp(exposedColor, vec3<f32>(0.0), vec3<f32>(1.0));
  }

  return acesFilmic(exposedColor);
}

fn bloomContribution(uv: vec2<f32>) -> vec3<f32> {
  if (postProcess.bloomSettings.x < 0.5) {
    return vec3<f32>(0.0);
  }

  return max(textureSample(bloomTexture, postSampler, uv).rgb * postProcess.bloomSettings.z, vec3<f32>(0.0));
}

fn sceneWithBloom(uv: vec2<f32>) -> vec3<f32> {
  return textureSample(sceneColorTexture, postSampler, uv).rgb + bloomContribution(uv);
}

fn dofCocPixels(linearDepth: f32) -> f32 {
  if (postProcess.dofSettings.x < 0.5 || linearDepth <= 0.0) {
    return 0.0;
  }

  let focusDistance = max(postProcess.dofSettings.y, 0.001);
  let focusRange = max(postProcess.dofSettings.z, 0.001);
  let maxBlurPixels = max(postProcess.dofSettings.w, 0.0);
  let focusError = max(abs(linearDepth - focusDistance) - focusRange, 0.0);
  return clamp(focusError / focusRange, 0.0, 1.0) * maxBlurPixels;
}

fn dofCocDebug(pixelPosition: vec2<f32>) -> vec3<f32> {
  let maxBlurPixels = max(postProcess.dofSettings.w, 0.001);
  let coc = dofCocPixels(linearDepthAtPixel(pixelPosition));
  return vec3<f32>(clamp(coc / maxBlurPixels, 0.0, 1.0));
}

fn dofBlurredSceneColor(uv: vec2<f32>, blurPixels: f32) -> vec3<f32> {
  let radius = max(blurPixels, 0.0);
  let texel = 1.0 / vec2<f32>(textureDimensions(sceneColorTexture));
  var color = sceneWithBloom(uv) * 0.28;
  color += sceneWithBloom(uv + texel * vec2<f32>(radius, 0.0)) * 0.11;
  color += sceneWithBloom(uv + texel * vec2<f32>(-radius, 0.0)) * 0.11;
  color += sceneWithBloom(uv + texel * vec2<f32>(0.0, radius)) * 0.11;
  color += sceneWithBloom(uv + texel * vec2<f32>(0.0, -radius)) * 0.11;
  color += sceneWithBloom(uv + texel * vec2<f32>(radius * 0.72, radius * 0.72)) * 0.07;
  color += sceneWithBloom(uv + texel * vec2<f32>(-radius * 0.72, radius * 0.72)) * 0.07;
  color += sceneWithBloom(uv + texel * vec2<f32>(radius * 0.72, -radius * 0.72)) * 0.07;
  color += sceneWithBloom(uv + texel * vec2<f32>(-radius * 0.72, -radius * 0.72)) * 0.07;
  return color;
}

fn fogFactor(linearDepth: f32) -> f32 {
  if (postProcess.fogSettings.x < 0.5 || linearDepth <= 0.0) {
    return 0.0;
  }

  let startDistance = max(postProcess.fogSettings.y, 0.0);
  let endDistance = max(postProcess.fogSettings.z, startDistance + 0.001);
  let density = clamp(postProcess.fogSettings.w, 0.0, 1.0);
  let curve = max(postProcess.fogColorAndCurve.w, 0.001);
  let linearFactor = clamp((linearDepth - startDistance) / (endDistance - startDistance), 0.0, 1.0);
  let easedFactor = smoothstep(0.0, 1.0, linearFactor);
  return clamp(pow(easedFactor, curve) * density, 0.0, 1.0);
}

fn fogFactorDebug(pixelPosition: vec2<f32>) -> vec3<f32> {
  let factor = fogFactor(linearDepthAtPixel(pixelPosition));
  return vec3<f32>(factor);
}

fn saturate(value: f32) -> f32 {
  return clamp(value, 0.0, 1.0);
}

fn safePow(value: f32, exponent: f32) -> f32 {
  return pow(max(value, 0.0), exponent);
}

fn skyHash2(value: vec2<f32>) -> f32 {
  return fract(sin(dot(value, vec2<f32>(127.1, 311.7))) * 43758.5453123);
}

fn skyValueNoise2(value: vec2<f32>) -> f32 {
  let cell = floor(value);
  let local = fract(value);
  let curve = local * local * (vec2<f32>(3.0) - 2.0 * local);
  let a = skyHash2(cell);
  let b = skyHash2(cell + vec2<f32>(1.0, 0.0));
  let c = skyHash2(cell + vec2<f32>(0.0, 1.0));
  let d = skyHash2(cell + vec2<f32>(1.0, 1.0));
  return mix(mix(a, b, curve.x), mix(c, d, curve.x), curve.y);
}

fn skyFbm2(value: vec2<f32>) -> f32 {
  var p = value;
  var amplitude = 0.5;
  var total = 0.0;
  for (var octave = 0; octave < 5; octave = octave + 1) {
    total += skyValueNoise2(p) * amplitude;
    p = p * 2.03 + vec2<f32>(17.2, 9.4);
    amplitude *= 0.5;
  }
  return total;
}

fn analyticSkyColor(ray: vec3<f32>, sunDirection: vec3<f32>) -> vec3<f32> {
  let sunElevation = camera.skyTimeAndLight.z;
  let turbidity = clamp(camera.skyAtmosphereAndCloud.x, 1.8, 8.0);
  let turbidityT = saturate((turbidity - 2.0) / 6.0);
  let daylight = smoothstep(-0.06, 0.18, sunElevation);
  let lowSun = (1.0 - smoothstep(0.0, 0.34, sunElevation)) * daylight;
  let up = saturate(ray.y);
  let horizon = 1.0 - smoothstep(0.0, 0.32, up);
  let sunDot = dot(ray, sunDirection);
  let antiSun = saturate(dot(ray, -sunDirection));
  let rayleighPhase = 0.75 * (1.0 + sunDot * sunDot);
  let miePhase = safePow(saturate(sunDot), mix(34.0, 9.0, turbidityT));
  let zenithDay = mix(
    vec3<f32>(0.08, 0.28, 0.78),
    vec3<f32>(0.24, 0.42, 0.70),
    turbidityT
  );
  let horizonDay = mix(
    vec3<f32>(0.62, 0.78, 0.96),
    vec3<f32>(1.20, 0.42, 0.18),
    lowSun
  );
  let sunsetZenith = mix(zenithDay, vec3<f32>(0.34, 0.30, 0.50), lowSun * 0.45);
  let warmHaze = mix(
    vec3<f32>(0.72, 0.80, 0.92),
    vec3<f32>(1.35, 0.42, 0.16),
    lowSun
  );
  let horizonHaze = warmHaze * (0.12 + turbidityT * 0.22 + lowSun * 0.42) * horizon;
  let sunWarmth = safePow(saturate(sunDot), 4.0) *
    lowSun *
    vec3<f32>(1.35, 0.42, 0.18) *
    (0.25 + horizon * 0.85);
  let base = mix(horizonDay, sunsetZenith, safePow(up, 0.42));
  let rayleigh = zenithDay * rayleighPhase * (0.16 + up * 0.24) * daylight;
  let mie = camera.sunColorAndAmbient.rgb * miePhase * (0.38 + turbidityT * 0.35) * daylight;
  let oppositeSoftening = vec3<f32>(0.08, 0.11, 0.18) * antiSun * (0.15 + up * 0.15) * daylight;
  return base * (0.45 + daylight * 0.72) + rayleigh + mie + horizonHaze + sunWarmth + oppositeSoftening;
}

fn sunRadiance(ray: vec3<f32>, sunDirection: vec3<f32>) -> vec3<f32> {
  let sunElevation = camera.skyTimeAndLight.z;
  let daylight = smoothstep(-0.04, 0.12, sunElevation);
  let sunDot = dot(ray, sunDirection);
  let glow = safePow(saturate(sunDot), 384.0) * 2.2;
  let wideGlow = safePow(saturate(sunDot), 18.0) * 0.22;
  let disk = smoothstep(0.99925, 0.99978, sunDot) * 8.0;
  return camera.sunColorAndAmbient.rgb * (wideGlow + glow + disk) * daylight;
}

fn cloudLayer(ray: vec3<f32>, sunDirection: vec3<f32>, skyColor: vec3<f32>) -> vec3<f32> {
  let rayHeight = max(ray.y, 0.0);
  let horizonFade = smoothstep(0.03, 0.20, rayHeight);
  let coverage = clamp(camera.skyAtmosphereAndCloud.y, 0.0, 1.0);
  if (coverage <= 0.0001) {
    return skyColor;
  }
  let speed = camera.skyAtmosphereAndCloud.z;
  let scale = camera.skyAtmosphereAndCloud.w;
  let softness = max(camera.skyCloudAndNight.x, 0.01);
  let shadow = clamp(camera.skyCloudAndNight.y, 0.0, 1.0);
  let nightBlend = camera.skyCloudAndNight.w;
  let wind = vec2<f32>(0.82, 0.37) * camera.skyTimeAndLight.x * speed;
  let cloudUv = ray.xz / max(ray.y + 0.14, 0.16) * scale + wind;
  let broad = skyFbm2(cloudUv * 0.72);
  let detail = skyFbm2(cloudUv * 2.15 + vec2<f32>(5.7, 11.3)) * 0.35;
  let cloudNoise = saturate(broad * 0.82 + detail);
  let density = smoothstep(1.0 - coverage, 1.0 - coverage + softness, cloudNoise) *
    horizonFade;
  let sunEdge = smoothstep(0.35, 0.98, dot(ray, sunDirection));
  let daylight = smoothstep(-0.04, 0.16, camera.skyTimeAndLight.z);
  let cloudLit = mix(
    vec3<f32>(0.50, 0.55, 0.65),
    camera.sunColorAndAmbient.rgb * vec3<f32>(1.05, 1.0, 0.92),
    0.55 + sunEdge * 0.35
  ) * (0.55 + daylight * 0.52);
  let cloudShade = skyColor * (1.0 - shadow * (0.45 + daylight * 0.30));
  let cloudColor = mix(cloudShade, cloudLit, 0.65 + sunEdge * 0.35);
  return mix(skyColor, cloudColor, density * 0.68 * (1.0 - nightBlend * 0.45));
}

fn starField(ray: vec3<f32>) -> vec3<f32> {
  let starIntensity = camera.skyTimeAndLight.w;
  let starUv = ray.xz / max(abs(ray.y) + 0.26, 0.26) * 150.0;
  let cell = floor(starUv);
  let local = fract(starUv) - vec2<f32>(0.5);
  let starSeed = skyHash2(cell);
  let starMask = smoothstep(0.985, 1.0, starSeed);
  let core = smoothstep(0.050, 0.0, length(local));
  let halo = smoothstep(0.115, 0.0, length(local)) * 0.18;
  let colorJitter = vec3<f32>(
    0.78 + skyHash2(cell + vec2<f32>(3.1, 9.2)) * 0.25,
    0.82 + skyHash2(cell + vec2<f32>(5.4, 2.8)) * 0.18,
    1.0
  );
  let brightness = 1.2 + skyHash2(cell + vec2<f32>(7.7, 4.2)) * 2.4;
  return colorJitter *
    starMask *
    (core + halo) *
    brightness *
    starIntensity *
    smoothstep(-0.08, 0.45, ray.y);
}

fn nightSkyColor(ray: vec3<f32>, sunDirection: vec3<f32>) -> vec3<f32> {
  let nightUp = saturate(ray.y * 0.75 + 0.25);
  var color = mix(vec3<f32>(0.006, 0.009, 0.020), vec3<f32>(0.018, 0.026, 0.055), nightUp);
  color += starField(ray);
  let moonDirection = normalize(-sunDirection);
  let moonVisibility = smoothstep(-0.02, 0.18, moonDirection.y);
  let moonDot = dot(ray, moonDirection);
  let moonDisk = smoothstep(0.9980, 0.9993, moonDot);
  let moonGlow = safePow(saturate(moonDot), 96.0) * 0.28;
  color += vec3<f32>(0.75, 0.82, 1.0) *
    (moonDisk * 2.4 + moonGlow) *
    camera.skyCloudAndNight.z *
    moonVisibility;
  return color;
}

fn skyColorAtUv(uv: vec2<f32>) -> vec3<f32> {
  let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
  let farWorldH = camera.inverseViewProjection * vec4<f32>(ndc, 1.0, 1.0);
  let farWorld = farWorldH.xyz / farWorldH.w;
  let ray = normalize(farWorld - camera.eyeWorld.xyz);
  let sunDirection = normalize(camera.sunDirectionAndIntensity.xyz);
  let daySky = analyticSkyColor(ray, sunDirection) + sunRadiance(ray, sunDirection);
  let cloudedDaySky = cloudLayer(ray, sunDirection, daySky);
  let nightSky = nightSkyColor(ray, sunDirection);
  return max(mix(cloudedDaySky, nightSky, camera.skyCloudAndNight.w), vec3<f32>(0.0));
}

fn applyFog(sceneColor: vec3<f32>, pixelPosition: vec2<f32>, uv: vec2<f32>) -> vec3<f32> {
  let factor = fogFactor(linearDepthAtPixel(pixelPosition));
  let tint = max(postProcess.fogColorAndCurve.rgb, vec3<f32>(0.0));
  let fogColor = skyColorAtUv(uv) * tint;
  return mix(sceneColor, fogColor, factor);
}

fn bloomBrightColor(color: vec3<f32>, threshold: f32) -> vec3<f32> {
  let brightness = max(color.r, max(color.g, color.b));
  let softRange = max(threshold * 0.5, 0.0001);
  let bloomWeight = smoothstep(threshold - softRange, threshold + softRange, brightness);
  return max(color, vec3<f32>(0.0)) * bloomWeight;
}

@fragment
fn bloomFragmentMain(input: PostVertexOutput) -> @location(0) vec4<f32> {
  if (bloomPostProcess.bloomSettings.x < 0.5) {
    return vec4<f32>(0.0, 0.0, 0.0, 1.0);
  }

  let threshold = bloomPostProcess.bloomSettings.y;
  let texel = 1.0 / vec2<f32>(textureDimensions(bloomSceneColorTexture));
  var bloomColor = bloomBrightColor(
    textureSample(bloomSceneColorTexture, bloomSampler, input.uv).rgb,
    threshold
  ) * 0.24;
  bloomColor += bloomBrightColor(
    textureSample(bloomSceneColorTexture, bloomSampler, input.uv + texel * vec2<f32>(1.5, 0.0)).rgb,
    threshold
  ) * 0.12;
  bloomColor += bloomBrightColor(
    textureSample(bloomSceneColorTexture, bloomSampler, input.uv + texel * vec2<f32>(-1.5, 0.0)).rgb,
    threshold
  ) * 0.12;
  bloomColor += bloomBrightColor(
    textureSample(bloomSceneColorTexture, bloomSampler, input.uv + texel * vec2<f32>(0.0, 1.5)).rgb,
    threshold
  ) * 0.12;
  bloomColor += bloomBrightColor(
    textureSample(bloomSceneColorTexture, bloomSampler, input.uv + texel * vec2<f32>(0.0, -1.5)).rgb,
    threshold
  ) * 0.12;
  bloomColor += bloomBrightColor(
    textureSample(bloomSceneColorTexture, bloomSampler, input.uv + texel * vec2<f32>(1.25, 1.25)).rgb,
    threshold
  ) * 0.07;
  bloomColor += bloomBrightColor(
    textureSample(bloomSceneColorTexture, bloomSampler, input.uv + texel * vec2<f32>(-1.25, 1.25)).rgb,
    threshold
  ) * 0.07;
  bloomColor += bloomBrightColor(
    textureSample(bloomSceneColorTexture, bloomSampler, input.uv + texel * vec2<f32>(1.25, -1.25)).rgb,
    threshold
  ) * 0.07;
  bloomColor += bloomBrightColor(
    textureSample(bloomSceneColorTexture, bloomSampler, input.uv + texel * vec2<f32>(-1.25, -1.25)).rgb,
    threshold
  ) * 0.07;

  return vec4<f32>(bloomColor, 1.0);
}

@fragment
fn postFragmentMain(input: PostVertexOutput) -> @location(0) vec4<f32> {
  let debugView = postProcess.debugViewAndScale.x;
  if (abs(debugView - POST_DEBUG_LINEAR_DEPTH) < 0.5) {
    return vec4<f32>(debugLinearDepth(input.clipPosition.xy), 1.0);
  }

  let sceneColor = textureSample(sceneColorTexture, postSampler, input.uv).rgb;
  if (abs(debugView - POST_DEBUG_SCENE_COLOR) < 0.5) {
    return vec4<f32>(max(sceneColor, vec3<f32>(0.0)), 1.0);
  }

  if (abs(debugView - POST_DEBUG_BLOOM) < 0.5) {
    let bloomColor = textureSample(bloomTexture, postSampler, input.uv).rgb;
    return vec4<f32>(max(bloomColor * postProcess.bloomSettings.z, vec3<f32>(0.0)), 1.0);
  }

  if (abs(debugView - POST_DEBUG_DOF_COC) < 0.5) {
    return vec4<f32>(dofCocDebug(input.clipPosition.xy), 1.0);
  }

  if (abs(debugView - POST_DEBUG_FOG_FACTOR) < 0.5) {
    return vec4<f32>(fogFactorDebug(input.clipPosition.xy), 1.0);
  }

  let sceneBloomColor = sceneColor + bloomContribution(input.uv);
  let dofBlurredView = abs(debugView - POST_DEBUG_DOF_BLURRED) < 0.5;
  var postEffectColor = sceneBloomColor;
  if (postProcess.dofSettings.x >= 0.5 || dofBlurredView) {
    let linearDepth = linearDepthAtPixel(input.clipPosition.xy);
    let dofRadiusPixels = dofCocPixels(linearDepth);
    let dofSceneColor = dofBlurredSceneColor(input.uv, dofRadiusPixels);
    if (dofBlurredView) {
      return vec4<f32>(applyToneMap(dofSceneColor), 1.0);
    }
    postEffectColor = dofSceneColor;
  }

  let foggedColor = applyFog(postEffectColor, input.clipPosition.xy, input.uv);
  let toneMappedColor = applyToneMap(foggedColor);
  if (abs(debugView - POST_DEBUG_POST_TONE_MAP) < 0.5) {
    return vec4<f32>(toneMappedColor, 1.0);
  }

  return vec4<f32>(toneMappedColor, 1.0);
}
