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

struct ObjectUniforms {
  world: mat4x4<f32>,
  normalWorld: mat4x4<f32>,
  albedoFactor: vec4<f32>,
  specularAndFactor: vec4<f32>,
  textureOptions: vec4<f32>,
};

@group(0) @binding(0) var<uniform> camera: Camera;
@group(1) @binding(0) var<uniform> object: ObjectUniforms;
@group(1) @binding(1) var albedoTexture: texture_2d_array<f32>;
@group(1) @binding(2) var normalTexture: texture_2d_array<f32>;
@group(1) @binding(3) var materialTexture: texture_2d_array<f32>;
@group(1) @binding(4) var albedoSampler: sampler;

const PI: f32 = 3.14159265359;
const MATERIAL_WORKFLOW_TERRAIN: f32 = 1.0;
const MATERIAL_WORKFLOW_METALLIC_ROUGHNESS: f32 = 2.0;
const MATERIAL_WORKFLOW_SPECULAR_GLOSSINESS: f32 = 3.0;

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
  viewDirection: vec3<f32>
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
  let direct = (diffuse + specular) * camera.sunColorAndAmbient.rgb * camera.sunDirectionAndIntensity.w * nDotL;
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
  let litColor =
    albedo * (camera.sunColorAndAmbient.w + diffuse * camera.sunColorAndAmbient.rgb) +
    object.specularAndFactor.rgb * camera.sunColorAndAmbient.rgb * specular;

  return vec4<f32>(litColor, object.albedoFactor.a);
}

fn shadeMetallicRoughness(input: VertexOutput, normal: vec3<f32>, viewDirection: vec3<f32>) -> vec4<f32> {
  let baseColor = sampleModelBaseColor(input);
  let metallicRoughness = sampleModelMetallicRoughness(input);
  let metallic = metallicRoughness.x;
  let roughness = metallicRoughness.y;
  let f0 = mix(vec3<f32>(0.04), baseColor.rgb, metallic);
  let diffuseColor = baseColor.rgb * (1.0 - metallic);
  let litColor = pbrDirectLight(diffuseColor, f0, roughness, normal, viewDirection);
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
  let litColor = pbrDirectLight(diffuseColor, specular, roughness, normal, viewDirection);
  return vec4<f32>(linearToSrgb(litColor), diffuse.a);
}

@fragment
fn fragmentMain(input: VertexOutput) -> @location(0) vec4<f32> {
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

fn saturate(value: f32) -> f32 {
  return clamp(value, 0.0, 1.0);
}

fn safePow(value: f32, exponent: f32) -> f32 {
  return pow(max(value, 0.0), exponent);
}

fn skyToneMap(color: vec3<f32>) -> vec3<f32> {
  let mapped = vec3<f32>(1.0) - exp(-max(color, vec3<f32>(0.0)) * 1.05);
  return pow(max(mapped, vec3<f32>(0.0)), vec3<f32>(1.0 / 2.2));
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

@fragment
fn skyFragmentMain(input: SkyVertexOutput) -> @location(0) vec4<f32> {
  let farWorldH = camera.inverseViewProjection * vec4<f32>(input.ndc, 1.0, 1.0);
  let farWorld = farWorldH.xyz / farWorldH.w;
  let ray = normalize(farWorld - camera.eyeWorld.xyz);
  let sunDirection = normalize(camera.sunDirectionAndIntensity.xyz);
  let daySky = analyticSkyColor(ray, sunDirection) + sunRadiance(ray, sunDirection);
  let cloudedDaySky = cloudLayer(ray, sunDirection, daySky);
  let nightSky = nightSkyColor(ray, sunDirection);
  let skyColor = mix(cloudedDaySky, nightSky, camera.skyCloudAndNight.w);
  return vec4<f32>(skyToneMap(skyColor), 1.0);
}
