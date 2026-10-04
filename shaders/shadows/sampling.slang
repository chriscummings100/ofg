// Outdoor frame ABI and portable depth-array PCF; only direct celestial lighting receives visibility.
struct OutdoorParameters
{
    float4 enabledExposure;
    float4 viewPositionNear;
    float4 viewForwardFar;
    float4 splitDistances;
    float4 texelSizes;
    float4 cloudSettings;
    float4 windLayers;
    float4 lightDirection;
    float4 shadowOptions;
    float4 ibl;
    float4 iblExposure; // Old/new generation exposure compensation during incremental updates.
    float4 viewport; // Width, height, aerial-perspective enabled, reserved.
    column_major float4x4 shadowMatrices[4];
};
ConstantBuffer<OutdoorParameters> outdoor;
DepthTexture2DArray shadowMaps;
SamplerComparisonState shadowSampler;
Texture2DArray<float4> aerialPerspective;
SamplerState environmentSampler;
// Selects by view-space distance; the last cascade fades to unshadowed light.
uint shadowCascadeIndex(float3 position)
{
    float distance = dot(position - outdoor.viewPositionNear.xyz, outdoor.viewForwardFar.xyz);
    return min(
        uint(distance > outdoor.splitDistances.x) + uint(distance > outdoor.splitDistances.y) +
            uint(distance > outdoor.splitDistances.z),
        3u
    );
}
// Uses geometric normal offset in world texels, then performs fixed 3x3 depth comparisons.
float cascadeVisibility(uint index, float3 position, float3 geometricNormal)
{
    float3 normal = dot(geometricNormal, outdoor.lightDirection.xyz) < 0 ? -geometricNormal : geometricNormal;
    position += normal * outdoor.texelSizes[index] * outdoor.enabledExposure.w;
    float4 clip = mul(outdoor.shadowMatrices[index], float4(position, 1));
    float3 p = clip.xyz / clip.w;
    p.xy = p.xy * float2(.5, -.5) + .5;
    if (any(p.xy < 0) || any(p.xy > 1) || p.z < 0 || p.z > 1)
    {
        return 1;
    }
    float result = 0;
    [unroll] for (int y = -1; y <= 1; ++y)[unroll] for (int x = -1; x <= 1; ++x) result +=
        shadowMaps.SampleCmpLevelZero(
            shadowSampler,
            float3(p.xy + float2(x, y) / outdoor.shadowOptions.x, index),
            p.z - outdoor.enabledExposure.z
        );
    return result / 9;
}
// Blends overlapping receiver bands and fades the final cascade without darkening IBL.
float outdoorVisibility(float3 position, float3 normal)
{
    if (outdoor.enabledExposure.x == 0)
    {
        return 1;
    }
    float visibility = 1;
    if (outdoor.shadowOptions.z > 0)
    {
        uint index = shadowCascadeIndex(position);
        float distance = dot(position - outdoor.viewPositionNear.xyz, outdoor.viewForwardFar.xyz);
        float end = outdoor.splitDistances[index];
        float start = index == 0 ? outdoor.viewPositionNear.w : outdoor.splitDistances[index - 1];
        float weight = smoothstep(end - (end - start) * outdoor.shadowOptions.y, end, distance);
        visibility = cascadeVisibility(index, position, normal);
        float next = index < 3 ? cascadeVisibility(min(index + 1, 3u), position, normal) : 1;
        visibility = lerp(visibility, next, weight);
    }
    return visibility *
           cloudTransmission(position, outdoor.lightDirection.xyz, outdoor.cloudSettings, outdoor.windLayers);
}
// Reconstructs the camera-volume depth coordinate, sampling only within the 32 initialized slices.
float3 applyAerialPerspective(float3 color, float3 position, float2 pixel)
{
    if (outdoor.enabledExposure.x == 0)
    {
        return color;
    }
    if (outdoor.viewport.z == 0)
    {
        return min(color * outdoor.enabledExposure.y, float3(65504));
    }
    float distance = min(length(position - outdoor.viewPositionNear.xyz), outdoor.lightDirection.w);
    float slice = sqrt(distance / outdoor.lightDirection.w) * 31;
    float lower = min(floor(slice), 30), upper = lower + 1;
    float nearDistance = lower * lower / (31 * 31) * outdoor.lightDirection.w;
    float farDistance = upper * upper / (31 * 31) * outdoor.lightDirection.w;
    // Samples store integrals at quadratic distances. Interpolate in metres, not fractional slice index;
    // otherwise the first interval incorrectly grows as sqrt(distance), exaggerating foreground haze.
    float weight = saturate((distance - nearDistance) / (farDistance - nearDistance));
    float2 uv = pixel / outdoor.viewport.xy;
    float4 a = aerialPerspective.SampleLevel(environmentSampler, float3(uv, lower), 0);
    float4 b = aerialPerspective.SampleLevel(environmentSampler, float3(uv, upper), 0);
    float4 fog = lerp(a, b, weight);
    // Physical lux and manual exposure can exceed FP16; clipping here avoids infinities in tone mapping.
    return min((color * (1 - fog.a) + fog.rgb) * outdoor.enabledExposure.y, float3(65504));
}
