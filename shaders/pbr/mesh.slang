// Mesh transforms, independent normal-map frames and surface texture channels for the portable PBR family.
struct DrawParameters
{
    column_major float4x4 clipFromLocal;
    column_major float4x4 worldFromLocal;
    column_major float4x4 normalFromLocal;
    float orientation;
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
// Reconstructs a tangent frame for this texture's transformed UVs, including mirrored UVs and UV1.
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
// Fully initializes factors before multiplying the glTF-defined texture channels.
Surface evaluateSurface(VertexOutput input, bool front)
{
    Surface s;
    // Derivatives are evaluated before any attribute-dependent branch, as required by WGSL uniformity.
    float3 qx = ddx(input.world), qy = ddy(input.world);
    float2 dx = ddx(input.uv), dy = ddy(input.uv);
    float4 base = material.baseColor * input.color;
#ifdef HAS_BASE_COLOR_TEXTURE
    base *= baseColorTexture.Sample(baseColorSampler, baseColorUv(input.uv, input.uv1));
#endif
    s.base = base.rgb;
    s.alpha = base.a;
    s.roughness = material.roughness;
    s.metallic = material.metallic;
#ifdef HAS_METALLIC_ROUGHNESS_TEXTURE
    float4 mr = metallicRoughnessTexture.Sample(metallicRoughnessSampler, metallicRoughnessUv(input.uv, input.uv1));
    s.roughness *= mr.g;
    s.metallic *= mr.b;
#endif
    float3 geometric = safeNormal(cross(qx, qy), float3(0, 0, -1));
    s.normal = dot(input.normal, input.normal) > 1e-12 ? normalize(input.normal) * (front ? 1 : -1) : geometric;
    s.coatNormal = s.normal;
#ifdef HAS_NORMAL_TEXTURE
    s.normal = mappedNormal(
        normalTexture.Sample(normalSampler, normalUv(input.uv, input.uv1)).rgb,
        material.normalScale,
        s.normal,
        input.world,
        normalUv(input.uv, input.uv1)
    );
#endif
#ifdef HAS_CLEARCOAT_NORMAL_TEXTURE
    s.coatNormal = mappedNormal(
        clearcoatNormalTexture.Sample(clearcoatNormalSampler, clearcoatNormalUv(input.uv, input.uv1)).rgb,
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
        occlusionTexture.Sample(occlusionSampler, occlusionUv(input.uv, input.uv1)).r,
        material.occlusionStrength
    );
#endif
    s.emission = material.emissive * material.emissiveStrength;
#ifdef HAS_EMISSIVE_TEXTURE
    s.emission *= emissiveTexture.Sample(emissiveSampler, emissiveUv(input.uv, input.uv1)).rgb;
#endif
    s.specular = material.specular;
    float3 specColor = material.specularColor;
#ifdef HAS_SPECULAR_TEXTURE
    s.specular *= specularTexture.Sample(specularSampler, specularUv(input.uv, input.uv1)).a;
#endif
#ifdef HAS_SPECULAR_COLOR_TEXTURE
    specColor *= specularColorTexture.Sample(specularColorSampler, specularColorUv(input.uv, input.uv1)).rgb;
#endif
    s.f0 = min(sq((material.ior - 1) / (material.ior + 1)) * specColor, float3(1));
    s.coat = material.clearcoat;
    s.coatRoughness = material.clearcoatRoughness;
#ifdef HAS_CLEARCOAT_TEXTURE
    s.coat *= clearcoatTexture.Sample(clearcoatSampler, clearcoatUv(input.uv, input.uv1)).r;
#endif
#ifdef HAS_CLEARCOAT_ROUGHNESS_TEXTURE
    s.coatRoughness *=
        clearcoatRoughnessTexture.Sample(clearcoatRoughnessSampler, clearcoatRoughnessUv(input.uv, input.uv1)).g;
#endif
    s.sheen = material.sheenColor;
    s.sheenRoughness = material.sheenRoughness;
#ifdef HAS_SHEEN_COLOR_TEXTURE
    s.sheen *= sheenColorTexture.Sample(sheenColorSampler, sheenColorUv(input.uv, input.uv1)).rgb;
#endif
#ifdef HAS_SHEEN_ROUGHNESS_TEXTURE
    s.sheenRoughness *= sheenRoughnessTexture.Sample(sheenRoughnessSampler, sheenRoughnessUv(input.uv, input.uv1)).a;
#endif
    s.iridescence = material.iridescence;
    s.filmIor = material.iridescenceIor;
    s.thickness = material.thicknessMaximum;
#ifdef HAS_IRIDESCENCE_TEXTURE
    s.iridescence *= iridescenceTexture.Sample(iridescenceSampler, iridescenceUv(input.uv, input.uv1)).r;
#endif
#ifdef HAS_IRIDESCENCE_THICKNESS_TEXTURE
    s.thickness = lerp(
        material.thicknessMinimum,
        material.thicknessMaximum,
        iridescenceThicknessTexture.Sample(iridescenceThicknessSampler, iridescenceThicknessUv(input.uv, input.uv1)).g
    );
#endif
    s.anisotropy = material.anisotropy;
    float2 direction = float2(1, 0);
#ifdef HAS_ANISOTROPY_TEXTURE
    float3 aniso = anisotropyTexture.Sample(anisotropySampler, anisotropyUv(input.uv, input.uv1)).rgb;
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
    color = shadeSurface(s, input.world);
    if (frame.displayOutput != 0 && frame.debugView == 0)
        color = frame.linearOutput != 0 ? color * frame.exposure : pbrNeutral(color * frame.exposure);
#endif
    if (frame.debugView == 1)
        color = s.normal * .5 + .5;
    if (frame.debugView == 2)
        color = float3(s.roughness);
    if (frame.debugView == 3)
        color = float3(s.metallic);
    if (frame.debugView == 4)
        color = s.base;
    // Late discard preserves derivative validity across mask boundaries.
    if (frame.alphaMode == 1 && s.alpha < material.alphaCutoff)
        discard;
    if (frame.displayOutput != 0 && frame.encodeSrgb != 0)
        color = linearToSrgb(color);
    return float4(color, frame.alphaMode == 2 ? s.alpha : 1);
}
