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
  let positions = array<vec2<f32>, 3>(
    vec2<f32>(-1.0, -1.0),
    vec2<f32>(3.0, -1.0),
    vec2<f32>(-1.0, 3.0)
  );

  var output: SkyVertexOutput;
  let position = positions[vertexIndex];
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
