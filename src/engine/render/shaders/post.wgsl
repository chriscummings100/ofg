struct PostVertexOutput {
  @builtin(position) clipPosition: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

struct PostProcessUniforms {
  debugViewAndScale: vec4<f32>,
  bloomSettings: vec4<f32>,
  dofSettings: vec4<f32>,
};

@group(0) @binding(0) var sceneColorTexture: texture_2d<f32>;
@group(0) @binding(1) var linearDepthTexture: texture_2d<f32>;
@group(0) @binding(2) var postSampler: sampler;
@group(0) @binding(3) var<uniform> postProcess: PostProcessUniforms;
@group(0) @binding(4) var bloomTexture: texture_2d<f32>;

@group(1) @binding(0) var bloomSceneColorTexture: texture_2d<f32>;
@group(1) @binding(1) var bloomSampler: sampler;
@group(1) @binding(2) var<uniform> bloomPostProcess: PostProcessUniforms;

const POST_DEBUG_FINAL: f32 = 0.0;
const POST_DEBUG_SCENE_COLOR: f32 = 1.0;
const POST_DEBUG_LINEAR_DEPTH: f32 = 2.0;
const POST_DEBUG_POST_TONE_MAP: f32 = 3.0;
const POST_DEBUG_BLOOM: f32 = 4.0;
const POST_DEBUG_DOF_COC: f32 = 5.0;
const POST_DEBUG_DOF_BLURRED: f32 = 6.0;

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

  let bloomColor = textureSample(bloomTexture, postSampler, input.uv).rgb;
  if (abs(debugView - POST_DEBUG_BLOOM) < 0.5) {
    return vec4<f32>(max(bloomColor * postProcess.bloomSettings.z, vec3<f32>(0.0)), 1.0);
  }

  let linearDepth = linearDepthAtPixel(input.clipPosition.xy);
  let dofRadiusPixels = dofCocPixels(linearDepth);
  if (abs(debugView - POST_DEBUG_DOF_COC) < 0.5) {
    return vec4<f32>(dofCocDebug(input.clipPosition.xy), 1.0);
  }

  let sceneBloomColor = sceneColor + bloomContribution(input.uv);
  let dofSceneColor = dofBlurredSceneColor(input.uv, dofRadiusPixels);
  if (abs(debugView - POST_DEBUG_DOF_BLURRED) < 0.5) {
    return vec4<f32>(applyToneMap(dofSceneColor), 1.0);
  }

  var postEffectColor = sceneBloomColor;
  if (postProcess.dofSettings.x >= 0.5) {
    postEffectColor = dofSceneColor;
  }

  let toneMappedColor = applyToneMap(postEffectColor);
  if (abs(debugView - POST_DEBUG_POST_TONE_MAP) < 0.5) {
    return vec4<f32>(toneMappedColor, 1.0);
  }

  return vec4<f32>(toneMappedColor, 1.0);
}
