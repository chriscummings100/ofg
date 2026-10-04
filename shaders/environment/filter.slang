// Matched Lambertian/GGX/Charlie importance filtering; equations follow the existing Khronos-derived bake.
TextureCube<float4> sourceEnvironment;
SamplerState filterSampler;
struct FilterSettings
{
    float4 faceFilterRoughnessSize;
};
ConstantBuffer<FilterSettings> filter;
struct FilterVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
// Produces top-left UV coordinates for one cube face.
[shader("vertex")] FilterVertex vertexMain(uint id : SV_VertexID)
{
    FilterVertex v;
    v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return v;
}
// Uses the same face convention as the baked environment and sky capture.
float3 filterDirection(uint face, float2 uv)
{
    float2 p = uv * 2 - 1;
    if (face == 0)
    {
        return normalize(float3(1, -p.y, -p.x));
    }
    if (face == 1)
    {
        return normalize(float3(-1, -p.y, p.x));
    }
    if (face == 2)
    {
        return normalize(float3(p.x, 1, p.y));
    }
    if (face == 3)
    {
        return normalize(float3(p.x, -1, -p.y));
    }
    if (face == 4)
    {
        return normalize(float3(p.x, -p.y, 1));
    }
    return normalize(float3(-p.x, -p.y, -1));
}
// Returns deterministic filtered incident radiance, with perceptual roughness mapped linearly over mips.
[shader("fragment")] float4 fragmentMain(FilterVertex input)
    : SV_Target
{
    uint kind = uint(filter.faceFilterRoughnessSize.y);
    float roughness = filter.faceFilterRoughnessSize.z;
    float3 n = filterDirection(uint(filter.faceFilterRoughnessSize.x), input.uv);
    if (kind == 1 && roughness == 0)
    {
        return sourceEnvironment.SampleLevel(filterSampler, n, 0);
    }
    float3 up = abs(n.y) > .999 ? float3(0, 0, 1) : float3(0, 1, 0), t = normalize(cross(up, n)), b = cross(n, t);
    float3 result = 0;
    float total = 0;
    float alpha = max(roughness * roughness, .0016);
    [loop] for (uint i = 0; i < 128; ++i)
    {
        float x = (i + .5) / 128, y = float(reversebits(i)) * 2.3283064365386963e-10;
        float c = kind == 0   ? sqrt(1 - y)
                  : kind == 1 ? sqrt((1 - y) / (1 + (alpha * alpha - 1) * y))
                              : sqrt(1 - pow(y, 2 * alpha / (2 * alpha + 1)));
        float s = sqrt(saturate(1 - c * c)), phi = 6.28318530718 * x;
        float3 h = float3(s * cos(phi), s * sin(phi), c);
        float3 l = kind == 0 ? h : float3(2 * h.z * h.xy, 2 * h.z * h.z - 1);
        float weight = kind == 0 ? 1 : max(l.z, 0);
        result += sourceEnvironment.SampleLevel(filterSampler, t * l.x + b * l.y + n * l.z, 0).rgb * weight;
        total += weight;
    }
    return float4(result / max(total, 1e-6), 1);
}
