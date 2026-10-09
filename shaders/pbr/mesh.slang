// Mesh transforms, independent normal-map frames and surface texture channels for the portable PBR family.
struct DrawParameters
{
    column_major float4x4 clipFromLocal;
    column_major float4x4 worldFromLocal;
    column_major float4x4 normalFromLocal;
    float orientation;
    uint alphaMode;
};
ConstantBuffer<DrawParameters> draw;
// WGSL uses numeric semantic indices as locations; these match the dedicated PBR RHI layout.
struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL1;
    float2 uv : TEXCOORD2;
    float4 tangent : TANGENT3;
    float2 uv1 : TEXCOORD4;
    float4 color : COLOR5;
};
struct VertexOutput
{
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 tangent : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float2 uv1 : TEXCOORD4;
    float4 color : COLOR;
};
// Uses inverse transpose normals, forward tangents and determinant-corrected tangent handedness.
[shader("vertex")] VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(draw.clipFromLocal, float4(input.position, 1));
    output.world = mul(draw.worldFromLocal, float4(input.position, 1)).xyz;
    output.normal = mul(draw.normalFromLocal, float4(input.normal, 0)).xyz;
    output.tangent =
        float4(mul(draw.worldFromLocal, float4(input.tangent.xyz, 0)).xyz, input.tangent.w * draw.orientation);
    output.uv = input.uv;
    output.uv1 = input.uv1;
    output.color = input.color;
    return output;
}
// Reconstructs a tangent scene.frame for this texture's transformed UVs, including mirrored UVs and UV1.
float3 mappedNormal(float3 texel, float scale, float3 n, float3 world, float2 uv)
{
    float3 qx = ddx(world), qy = ddy(world);
    float2 dx = ddx(uv), dy = ddy(uv);
    float determinant = dx.x * dy.y - dx.y * dy.x;
    if (abs(determinant) < 1e-10)
        return n;
    float3 t = safeNormal((qx * dy.y - qy * dx.y) / determinant, float3(1, 0, 0));
    t = safeNormal(t - n * dot(n, t), float3(1, 0, 0));
    float3 rawB = (qy * dx.x - qx * dy.x) / determinant;
    float signB = dot(cross(n, t), rawB) < 0 ? -1 : 1;
    float3 b = cross(n, t) * signB;
    float3 tn = texel * 2 - 1;
    tn.xy *= scale;
    return safeNormal(t * tn.x + b * tn.y + n * tn.z, n);
}
#ifdef HAS_TERRAIN_TEXTURE
// Samples the two-component height representation without requiring float32 texture filtering.
float terrainHeight(float2 uv)
{
    float2 height = material.terrainTexture.SampleLevel(material.terrainSampler, uv, 0).xy;
    return (height.x + height.y) * 1024;
}
#endif
// Evaluates glTF factors and optional terrain source detail in translation-only terrain coordinates.
Surface evaluateSurface(VertexOutput input, bool front)
{
    Surface s;
    // Derivatives are evaluated before any attribute-dependent branch, as required by WGSL uniformity.
    float3 qx = ddx(input.world), qy = ddy(input.world);
    float2 dx = ddx(input.uv), dy = ddy(input.uv);
    float4 base = material.baseColor * input.color;
#ifdef HAS_BASE_COLOR_TEXTURE
    base *= material.baseColorTexture.Sample(material.baseColorSampler, baseColorUv(input.uv, input.uv1));
#endif
    s.base = base.rgb;
    s.alpha = base.a;
    s.roughness = material.roughness;
    s.metallic = material.metallic;
#ifdef HAS_METALLIC_ROUGHNESS_TEXTURE
    float4 mr = material.metallicRoughnessTexture.Sample(
        material.metallicRoughnessSampler,
        metallicRoughnessUv(input.uv, input.uv1)
    );
    s.roughness *= mr.g;
    s.metallic *= mr.b;
#endif
    float3 geometric = safeNormal(cross(qx, qy), float3(0, 0, -1));
    s.normal = dot(input.normal, input.normal) > 1e-12 ? normalize(input.normal) * (front ? 1 : -1) : geometric;
    s.coatNormal = s.normal;
#ifdef HAS_TERRAIN_TEXTURE
    float2 terrainUv = input.uv * material.terrainMapping.xy + material.terrainMapping.zw;
    float2 texel = float2(1.0 / 259, 0);
    float hx = terrainHeight(terrainUv + texel) - terrainHeight(terrainUv - texel);
    float hz = terrainHeight(terrainUv + texel.yx) - terrainHeight(terrainUv - texel.yx);
    s.normal = normalize(float3(-hx, 2 * material.terrainSpacing, -hz));
    s.coatNormal = s.normal;
    float4 attributes = material.terrainTexture.SampleLevel(material.terrainSampler, terrainUv, 0);
    int2 coordinate = clamp(int2(floor(terrainUv * 259 - .5)), int2(0), int2(257));
    uint validity = uint(round(material.terrainValidity.Load(int3(coordinate, 0)) * 255)) &
                    uint(round(material.terrainValidity.Load(int3(coordinate + int2(1, 0), 0)) * 255)) &
                    uint(round(material.terrainValidity.Load(int3(coordinate + int2(0, 1), 0)) * 255)) &
                    uint(round(material.terrainValidity.Load(int3(coordinate + int2(1, 1), 0)) * 255));
    uint nearestValidity = uint(
        round(material.terrainValidity.Load(int3(clamp(int2(floor(terrainUv * 259)), int2(0), int2(258)), 0)) * 255)
    );
    if ((validity & 1) == 0)
        attributes.z = -1;
    if ((validity & 2) == 0)
        attributes.w = -1;
    float sediment = max(attributes.z * 1024, 0), water = max(attributes.w * 1024, 0);
    s.base = lerp(float3(.3, .32, .28), float3(.32, .2, .09), saturate(sediment));
    // Static submerged-ground tint makes the sea-level crossing inspectable without simulating water.
    s.base = lerp(s.base, float3(.025, .18, .23), saturate(water / 8));
    if (material.terrainChannel == 1)
        s.base = float3(saturate(terrainHeight(terrainUv) / 100 + .5));
    if (material.terrainChannel == 2)
        s.base = attributes.z < 0 ? float3(1, 0, 1) : float3(saturate(sediment / 10));
    if (material.terrainChannel == 3)
        s.base = attributes.w < 0 ? float3(1, 0, 1) : float3(0, saturate(water / 100), 1);
    // V1's sole material is bare rock (ID 1); categorical validity uses nearest sampling, never filtering.
    if (material.terrainChannel == 4)
        s.base = (nearestValidity & 4) == 0 ? float3(1, 0, 1) : float3(.3, .32, .28);
#endif
#ifdef HAS_NORMAL_TEXTURE
    s.normal = mappedNormal(
        material.normalTexture.Sample(material.normalSampler, normalUv(input.uv, input.uv1)).rgb,
        material.normalScale,
        s.normal,
        input.world,
        normalUv(input.uv, input.uv1)
    );
#endif
#ifdef HAS_CLEARCOAT_NORMAL_TEXTURE
    s.coatNormal = mappedNormal(
        material.clearcoatNormalTexture.Sample(material.clearcoatNormalSampler, clearcoatNormalUv(input.uv, input.uv1))
            .rgb,
        material.clearcoatNormalScale,
        s.coatNormal,
        input.world,
        clearcoatNormalUv(input.uv, input.uv1)
    );
#endif
    s.tangent = safeNormal(
        input.tangent.xyz - s.normal * dot(s.normal, input.tangent.xyz),
        safeNormal(cross(float3(0, 1, 0), s.normal), float3(1, 0, 0))
    );
    float handedness = input.tangent.w;
    if (abs(handedness) < 0.5)
    {
        float determinant = dx.x * dy.y - dx.y * dy.x;
        if (abs(determinant) > 1e-10)
        {
            float3 t = (qx * dy.y - qy * dx.y) / determinant;
            float3 b = (qy * dx.x - qx * dy.x) / determinant;
            s.tangent = safeNormal(t - s.normal * dot(s.normal, t), s.tangent);
            handedness = dot(cross(s.normal, s.tangent), b) < 0 ? -1 : 1;
        }
        else
            handedness = 1;
    }
    s.bitangent = cross(s.normal, s.tangent) * handedness;
    s.ao = 1;
#ifdef HAS_OCCLUSION_TEXTURE
    s.ao = lerp(
        1,
        material.occlusionTexture.Sample(material.occlusionSampler, occlusionUv(input.uv, input.uv1)).r,
        material.occlusionStrength
    );
#endif
    s.emission = material.emissive * material.emissiveStrength;
#ifdef HAS_EMISSIVE_TEXTURE
    s.emission *= material.emissiveTexture.Sample(material.emissiveSampler, emissiveUv(input.uv, input.uv1)).rgb;
#endif
    s.specular = material.specular;
    float3 specColor = material.specularColor;
#ifdef HAS_SPECULAR_TEXTURE
    s.specular *= material.specularTexture.Sample(material.specularSampler, specularUv(input.uv, input.uv1)).a;
#endif
#ifdef HAS_SPECULAR_COLOR_TEXTURE
    specColor *=
        material.specularColorTexture.Sample(material.specularColorSampler, specularColorUv(input.uv, input.uv1)).rgb;
#endif
    s.f0 = min(sq((material.ior - 1) / (material.ior + 1)) * specColor, float3(1));
    s.coat = material.clearcoat;
    s.coatRoughness = material.clearcoatRoughness;
#ifdef HAS_CLEARCOAT_TEXTURE
    s.coat *= material.clearcoatTexture.Sample(material.clearcoatSampler, clearcoatUv(input.uv, input.uv1)).r;
#endif
#ifdef HAS_CLEARCOAT_ROUGHNESS_TEXTURE
    s.coatRoughness *= material.clearcoatRoughnessTexture
                           .Sample(material.clearcoatRoughnessSampler, clearcoatRoughnessUv(input.uv, input.uv1))
                           .g;
#endif
    s.sheen = material.sheenColor;
    s.sheenRoughness = material.sheenRoughness;
#ifdef HAS_SHEEN_COLOR_TEXTURE
    s.sheen *= material.sheenColorTexture.Sample(material.sheenColorSampler, sheenColorUv(input.uv, input.uv1)).rgb;
#endif
#ifdef HAS_SHEEN_ROUGHNESS_TEXTURE
    s.sheenRoughness *=
        material.sheenRoughnessTexture.Sample(material.sheenRoughnessSampler, sheenRoughnessUv(input.uv, input.uv1)).a;
#endif
    s.iridescence = material.iridescence;
    s.filmIor = material.iridescenceIor;
    s.thickness = material.thicknessMaximum;
#ifdef HAS_IRIDESCENCE_TEXTURE
    s.iridescence *=
        material.iridescenceTexture.Sample(material.iridescenceSampler, iridescenceUv(input.uv, input.uv1)).r;
#endif
#ifdef HAS_IRIDESCENCE_THICKNESS_TEXTURE
    s.thickness = lerp(
        material.thicknessMinimum,
        material.thicknessMaximum,
        material.iridescenceThicknessTexture
            .Sample(material.iridescenceThicknessSampler, iridescenceThicknessUv(input.uv, input.uv1))
            .g
    );
#endif
    s.anisotropy = material.anisotropy;
    float2 direction = float2(1, 0);
#ifdef HAS_ANISOTROPY_TEXTURE
    float3 aniso = material.anisotropyTexture.Sample(material.anisotropySampler, anisotropyUv(input.uv, input.uv1)).rgb;
    s.anisotropy *= aniso.b;
    float2 encoded = aniso.rg * 2 - 1;
    direction = dot(encoded, encoded) > 1e-8 ? normalize(encoded) : float2(1, 0);
#endif
    float c = cos(material.anisotropyRotation), sn = sin(material.anisotropyRotation);
    direction = float2(c * direction.x - sn * direction.y, sn * direction.x + c * direction.y);
    float3 t = s.tangent * direction.x + s.bitangent * direction.y;
    s.bitangent = s.bitangent * direction.x - s.tangent * direction.y;
    s.tangent = t;
    // Authored zero remains visible in debug; evaluation is regularized to avoid a delta distribution on finite pixels.
    s.roughness = clamp(s.roughness, 0.045, 1);
    s.coatRoughness = clamp(s.coatRoughness, 0.045, 1);
    s.sheenRoughness = clamp(s.sheenRoughness, 0.045, 1);
    return s;
}
// Outputs HDR for lit opaque surfaces, or display-linear color for the sorted composition pass.
[shader("fragment")] float4 fragmentMain(VertexOutput input, bool front : SV_IsFrontFace)
    : SV_Target
{
    Surface s = evaluateSurface(input, front);
    float3 color;
#ifdef UNLIT
    color = s.base;
#else
    float3 geometricNormal = safeNormal(cross(ddx(input.world), ddy(input.world)), s.normal);
    color = shadeSurface(s, input.world, geometricNormal);
    color = applyAerialPerspective(color, input.world, input.position.xy);
    if (scene.outdoor.enabledExposure.x > 0 && scene.outdoor.shadowOptions.w == 1)
    {
        uint cascade = shadowCascadeIndex(input.world);
        color = cascade == 0   ? float3(1, .1, .1)
                : cascade == 1 ? float3(.1, 1, .1)
                : cascade == 2 ? float3(.1, .1, 1)
                               : float3(1, 1, .1);
    }
    if (scene.outdoor.enabledExposure.x > 0 && scene.outdoor.shadowOptions.w == 2)
        color = float3(outdoorVisibility(input.world, geometricNormal));
    if (scene.frame.displayOutput != 0 && scene.frame.debugView == 0)
        color = scene.frame.linearOutput != 0 ? color * scene.frame.exposure : pbrNeutral(color * scene.frame.exposure);
#endif
    if (scene.frame.debugView == 1)
        color = s.normal * .5 + .5;
    if (scene.frame.debugView == 2)
        color = float3(s.roughness);
    if (scene.frame.debugView == 3)
        color = float3(s.metallic);
    if (scene.frame.debugView == 4)
        color = s.base;
    // Late discard preserves derivative validity across mask boundaries.
    if (draw.alphaMode == 1 && s.alpha < material.alphaCutoff)
        discard;
    if (scene.frame.displayOutput != 0 && scene.frame.encodeSrgb != 0)
        color = linearToSrgb(color);
    return float4(color, draw.alphaMode == 2 ? s.alpha : 1);
}
