// Portable depth-array PCF and aerial perspective using the shared scene block.
// Selects by view-space distance; the last cascade fades to unshadowed light.
uint shadowCascadeIndex(float3 position)
{
    float distance = dot(position - scene.outdoor.viewPositionNear.xyz, scene.outdoor.viewForwardFar.xyz);
    return min(
        uint(distance > scene.outdoor.splitDistances.x) + uint(distance > scene.outdoor.splitDistances.y) +
            uint(distance > scene.outdoor.splitDistances.z),
        3u
    );
}
// Uses geometric normal offset in world texels, then performs fixed 3x3 depth comparisons.
float cascadeVisibility(uint index, float3 position, float3 geometricNormal)
{
    float3 normal = dot(geometricNormal, scene.outdoor.lightDirection.xyz) < 0 ? -geometricNormal : geometricNormal;
    position += normal * scene.outdoor.texelSizes[index] * scene.outdoor.enabledExposure.w;
    float4 clip = mul(scene.outdoor.shadowMatrices[index], float4(position, 1));
    float3 p = clip.xyz / clip.w;
    p.xy = p.xy * float2(.5, -.5) + .5;
    if (any(p.xy < 0) || any(p.xy > 1) || p.z < 0 || p.z > 1)
    {
        return 1;
    }
    float result = 0;
    [unroll] for (int y = -1; y <= 1; ++y)[unroll] for (int x = -1; x <= 1; ++x) result +=
        scene.shadowMaps.SampleCmpLevelZero(
            scene.shadowSampler,
            float3(p.xy + float2(x, y) / scene.outdoor.shadowOptions.x, index),
            p.z - scene.outdoor.enabledExposure.z
        );
    return result / 9;
}
// Blends overlapping receiver bands and fades the final cascade without darkening IBL.
float outdoorVisibility(float3 position, float3 normal)
{
    if (scene.outdoor.enabledExposure.x == 0)
    {
        return 1;
    }
    float visibility = 1;
    if (scene.outdoor.shadowOptions.z > 0)
    {
        uint index = shadowCascadeIndex(position);
        float distance = dot(position - scene.outdoor.viewPositionNear.xyz, scene.outdoor.viewForwardFar.xyz);
        float end = scene.outdoor.splitDistances[index];
        float start = index == 0 ? scene.outdoor.viewPositionNear.w : scene.outdoor.splitDistances[index - 1];
        float weight = smoothstep(end - (end - start) * scene.outdoor.shadowOptions.y, end, distance);
        visibility = cascadeVisibility(index, position, normal);
        float next = index < 3 ? cascadeVisibility(min(index + 1, 3u), position, normal) : 1;
        visibility = lerp(visibility, next, weight);
    }
    return visibility * cloudTransmission(
                            position,
                            scene.outdoor.lightDirection.xyz,
                            scene.outdoor.cloudSettings,
                            scene.outdoor.windLayers
                        );
}
// Reconstructs the camera-volume depth coordinate, sampling only within the 32 initialized slices.
float3 applyAerialPerspective(float3 color, float3 position, float2 pixel)
{
    if (scene.outdoor.enabledExposure.x == 0)
    {
        return color;
    }
    if (scene.outdoor.viewport.z == 0)
    {
        return min(color * scene.outdoor.enabledExposure.y, float3(65504));
    }
    float distance = min(length(position - scene.outdoor.viewPositionNear.xyz), scene.outdoor.lightDirection.w);
    float slice = sqrt(distance / scene.outdoor.lightDirection.w) * 31;
    float lower = min(floor(slice), 30), upper = lower + 1;
    float nearDistance = lower * lower / (31 * 31) * scene.outdoor.lightDirection.w;
    float farDistance = upper * upper / (31 * 31) * scene.outdoor.lightDirection.w;
    // Samples store integrals at quadratic distances. Interpolate in metres, not fractional slice index;
    // otherwise the first interval incorrectly grows as sqrt(distance), exaggerating foreground haze.
    float weight = saturate((distance - nearDistance) / (farDistance - nearDistance));
    float2 uv = pixel / scene.outdoor.viewport.xy;
    float4 a = scene.aerialPerspective.SampleLevel(scene.environmentSampler, float3(uv, lower), 0);
    float4 b = scene.aerialPerspective.SampleLevel(scene.environmentSampler, float3(uv, upper), 0);
    float4 fog = lerp(a, b, weight);
    // Physical lux and manual exposure can exceed FP16; clipping here avoids infinities in tone mapping.
    return min((color * (1 - fog.a) + fog.rgb) * scene.outdoor.enabledExposure.y, float3(65504));
}
