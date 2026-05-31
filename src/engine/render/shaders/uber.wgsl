struct Camera {
  viewProjection: mat4x4<f32>,
  inverseViewProjection: mat4x4<f32>,
  eyeWorld: vec4<f32>,
  sunDirectionAndIntensity: vec4<f32>,
  sunColorAndAmbient: vec4<f32>,
};

struct ObjectUniforms {
  world: mat4x4<f32>,
  albedoFactor: vec4<f32>,
  specularAndFactor: vec4<f32>,
};

@group(0) @binding(0) var<uniform> camera: Camera;
@group(1) @binding(0) var<uniform> object: ObjectUniforms;

struct VertexInput {
  @location(0) position: vec3<f32>,
  @location(1) color: vec3<f32>,
};

struct VertexOutput {
  @builtin(position) clipPosition: vec4<f32>,
  @location(0) color: vec3<f32>,
  @location(1) worldPosition: vec3<f32>,
};

@vertex
fn vertexMain(input: VertexInput) -> VertexOutput {
  var output: VertexOutput;
  let worldPosition = object.world * vec4<f32>(input.position, 1.0);
  output.clipPosition = camera.viewProjection * worldPosition;
  output.color = input.color;
  output.worldPosition = worldPosition.xyz;
  return output;
}

@fragment
fn fragmentMain(input: VertexOutput) -> @location(0) vec4<f32> {
  let viewDirection = normalize(camera.eyeWorld.xyz - input.worldPosition);
  var normal = normalize(cross(dpdx(input.worldPosition), dpdy(input.worldPosition)));
  if (dot(normal, viewDirection) < 0.0) {
    normal = -normal;
  }

  let lightDirection = normalize(camera.sunDirectionAndIntensity.xyz);
  let halfDirection = normalize(lightDirection + viewDirection);
  let diffuse = max(dot(normal, lightDirection), 0.0) * camera.sunDirectionAndIntensity.w;
  let specular = pow(max(dot(normal, halfDirection), 0.0), 32.0) *
    object.specularAndFactor.w *
    camera.sunDirectionAndIntensity.w;
  let albedo = input.color * object.albedoFactor.rgb;
  let litColor =
    albedo * (camera.sunColorAndAmbient.w + diffuse * camera.sunColorAndAmbient.rgb) +
    object.specularAndFactor.rgb * camera.sunColorAndAmbient.rgb * specular;

  return vec4<f32>(litColor, object.albedoFactor.a);
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
